
#include <gj_lib.h>
#include <gj_linux.cc>

#include <stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
// #include <netinet/in.h>
// #include <fcntl.h>

gjMemStack stringStack = gjInitMemStack(megabytes(1));

class String {
public:
	char *mem;
	int len;

	String () {}

	String (char *str) {
		this->mem = str; // todo: this doesn't copy
		this->len = gjStrlen(str);
	}
};

String stringConcatOnStack (String str1, String str2) {
	String result;
	result.len = str1.len + str2.len;
	result.mem = gjPushMemStack(&stringStack, result.len + 1, true);
	gjMemcpy(result.mem, str1.mem, str1.len);
	gjMemcpy(result.mem + str1.len, str2.mem, str2.len);
	return result;
}

String operator+ (String str1, String str2) {
	return stringConcatOnStack(str1, str2);
}

String operator+ (String str1, char *str2) {
	String string2 = str2;
	return stringConcatOnStack(str1, string2);
}

String operator+ (String str, int num) {
	char buffer[64];
	snprintf(buffer, 64, "%i", num);
	String str2 = buffer;
	return stringConcatOnStack(str, str2);
}

void operator+= (String &str1, String str2) {
	str1 = stringConcatOnStack(str1, str2);
}

void operator+= (String &str1, char *str2) {
	String s2 = str2;
	str1 = stringConcatOnStack(str1, s2);
}

void gjStringPrint (String str) {
	printf("%s \n", str.mem);
}

void stringDebugPrint (String str) {
	printf("string: {\n");
	printf("	data: %s\n", str.mem);
	printf("	length: %i\n", str.len);
	printf("}\n\n");
}

int netStreamConnect (const char *url, const char *port) {
	addrinfo addressHints = {};
	addressHints.ai_family = AF_UNSPEC;
	addressHints.ai_socktype = SOCK_STREAM;

	int tcpThing = IPPROTO_TCP;

	addrinfo *suggestedAddrInfo;
	int result = getaddrinfo(url, port, &addressHints, &suggestedAddrInfo);
	if (result != 0) {
		printf("getaddrinfo error \n\n");
	}

	int socketHandle = -1;
	addrinfo *addr = suggestedAddrInfo;
	while (addr != NULL) {
		socketHandle = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
		if (socketHandle != -1) {
			break;
		}

		addr = addr->ai_next;
	}
	
	if (socketHandle == -1) {
		printf("Couldn't create socket \n\n");
	}

	char str[1024];
	inet_ntop(addr->ai_family, &(((sockaddr_in*)addr->ai_addr)->sin_addr), str, sizeof(str));
	printf("Connecting to %s \n\n", str);

	result = connect(socketHandle, addr->ai_addr, addr->ai_addrlen);
	if (result == -1) {
		printf("Couldn't connect \n\n");
	}

	/*{
		hostent *host;
		host = gethostbyname(url);
		if (!host) {
			printf("gethostbyname failed \n\n");
		}

		sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(6667);
		addr.sin_addr = *((in_addr*)host->h_addr);

		int connectResult = connect(socketHandle, (sockaddr*)&addr, sizeof(addr));
		if (connectResult == -1) {
			printf("connect failed \n\n");
		}
	}*/

	freeaddrinfo(suggestedAddrInfo);

	// fcntl(socketHandle, F_SETFL, O_NONBLOCK);
	// fcntl(socketHandle, F_SETFL, O_ASYNC);

	return socketHandle;
}

void *netRead (int socket, gjMemStack *memStack) {
	const int bufferSize = 1024;
	char *buffer = gjPushMemStack(memStack, bufferSize, true);
	int readSize = read(socket, buffer, bufferSize-1);
	if (readSize == -1) {
		printf("Read error \n\n");
	}
	if (readSize <= 0) {
		return NULL;
	}

	void *stackMemory = gjPushMemStack(memStack, readSize, true);
	gjMemcpy(stackMemory, buffer, readSize);

	printf("Response \n%s \n\n", buffer);

	gjPopMemStack(memStack, bufferSize);

	return stackMemory;
}

void netWrite (int socket, const char *data) {
	// const char *data = "NICK Matt_Test\r\n";
	int dataLen = gjStrlen(data);
	printf("%s \n\n", data);

	int result = write(socket, data, dataLen);
	if (result != dataLen) {
		printf("Write error \n\n");
	}
}

gjMemStack memStack = gjInitMemStack(megabytes(1));
gjMemStack inputMemStack = gjInitMemStack(kilobytes(1));


enum ClientCommandType {
	COMMAND_MSG,
	COMMAND_QUIT,
	COMMAND_JOIN,
};

struct ClientCommand {
	// ClientCommandType type;
	// union {
	// 	struct {
	// 		char *str;
	// 	} msg;
	// 	struct {
	// 		char *msg;
	// 	} quit;
	// 	struct {
	// 		char *channel;
	// 	} join;
	// };

	char *msg;

	char *name;
	char *args[16];
};

char *getWord (char **str) {
	char *result = NULL;
	char *s = *str;
	bool colonArg = false;

	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
		++s;
	}
	if (*s != 0) {
		if (*s == ':') {
			colonArg = true;
			++s;
		}
		result = s;
	}

	int len = 0;
	if (result) {
		if (colonArg) {
			while (*s != '\r' && *s != '\n' && *s != 0) {
				++len;
				++s;
			}
		} else {
			while (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' && *s != 0) {
				++len;
				++s;
			}
		}
		
		char *mem = gjPushMemStack(&inputMemStack, len+1);
		gjMemcpy(mem, result, len);
		mem[len] = 0;
		result = mem;
	}

	*str = s;
	return result;
}

ClientCommand parseInput (char *input) {
	ClientCommand command = {};

	int size = gjStrlen(input);
	char *s = input;
	if (*s == '/') {
		++s;
		command.name = getWord(&s);

		fiz (15) {
			char *arg = getWord(&s);
			if (arg) {
				command.args[i] = arg;
			} else {
				break;
			}
		}

		/*if (gjStrcmp(cmdStr, "quit") == 0) {
			command.type = COMMAND_QUIT;
			char *msg = getWord(&s);
			if (msg) {
				command.quit.msg = msg;
			}
		}

		if (gjStrcmp(cmdStr, "join") == 0) {
			command.type = COMMAND_JOIN;
			char *channel = getWord(&s);
			if (channel) {
				command.join.channel = channel;
			}
		}*/
	} else {
		printf("msg \n\n");
		command.msg = input; // todo: maybe move this into stack memory
	}

	return command;
}

void *clientInputThread (void *arg) {
	int socket = *(int*)arg;

	while (true) {
		const int bufferSize = 1024;
		char str[bufferSize];
		gjClearMem(str, bufferSize);

		int size = read(STDIN_FILENO, str, bufferSize-1);
		str[size] = 0;

		ClientCommand command = parseInput(str);
		printf("cmd: %s \n", command.name);
		fiz (15) {
			if (command.args[i]) {
				printf("	arg: %s \n", command.args[i]);
			} else {
				break;
			}
		}

		if (command.msg) {
			char str[1024];
			sprintf(str, "%s\r\n", command.msg);
			netWrite(socket, str);
		}
		if (gjEqual(command.name, "quit")) {
			char str[1024];
			if (command.args[0]) {
				sprintf(str, "QUIT :%s\r\n", command.args[0]);
			} else {
				sprintf(str, "QUIT\r\n");
			}
			netWrite(socket, str);
		}
		if (gjEqual(command.name, "join")) {
			char str[1024];
			if (command.args[0]) {
				sprintf(str, "JOIN %s\r\n", command.args[0]);
				netWrite(socket, str);
			} else {
				printf("You must specify a channel name \n\n");
			}
		}

		/*if (command.type == COMMAND_MSG) {
			char str[1024];
			sprintf(str, "%s\r\n", command.msg.str);
			netWrite(socket, str);
		}
		if (command.type == COMMAND_QUIT) {
			char str[1024];
			if (command.quit.msg) {
				sprintf(str, "QUIT :%s\r\n", command.quit.msg);
			} else {
				sprintf(str, "QUIT\r\n");
			}
			netWrite(socket, str);
		}
		if (command.type == COMMAND_JOIN) {
			char str[1024];
			if (command.join.channel) {
				sprintf(str, "JOIN %s\r\n", command.quit.msg);
				netWrite(socket, str);
			} else {
				printf("You must specify a channel name \n\n");
			}
		}*/
	}

	return arg;
}

void testConnect () {
	const char *url = /*"54.93.169.107"*/"irc.afternet.org";
	const char *port = "6667";

	int socket = netStreamConnect(url, port);

	pthread_t thread;
	pthread_attr_t threadAttr;
	pthread_create(&thread, NULL, clientInputThread, &socket);

	netWrite(socket, "HELLO\r\n");
	netWrite(socket, "NICK matt123\r\n");
	netWrite(socket, "USER matt123 0 * :Matt H\r\n");
	
	while (true) {
		char *data = (char*)netRead(socket, &memStack);
		if (!data) {
			break;
		}
		
		if (gjStrcmp(data, "NOTICE AUTH :*** Found your hostname\r\n") == 0 ||
			gjStrcmp(data, "NOTICE * :*** Found your hostname\r\n") == 0) {
		}

		if (data[0] == 'P' && data[1] == 'I' && data[2] == 'N' && data[3] == 'G') {
			data[1] = 'O';
			for (int i = 0; i < gjStrlen(data); ++i) {
				if (data[i] == '\n') {
					data[i+1] = 0;
					break;
				}
			}
			netWrite(socket, data);
		}
	}

	/*while (true) {
		char str[1024];
		gjClearMem(str, 1024);

		int size = read(STDIN_FILENO, str, 1024);
		str[size] = 0;
		netWrite(socket, str);
	}*/

	printf("Closing socket... \n\n");
	close(socket);
}

int main () {
	printf("Irc client... \n\n");

	testConnect();

	

	// String test = (String) "Hello World!" + "...";
	// String asd = test + " fuck";
	// stringDebugPrint(test);
	// stringDebugPrint(asd);

	// String name = (String) "Matt " + "Hartley";
	// String welcome = (String) "Welcome back to irc, " + name + "! after " + 27 + " days away.";
	// welcome += (String) "\n" + "How are you? ";
	// welcome += name;
	// stringDebugPrint(welcome);
}