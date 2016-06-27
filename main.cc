
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
	ClientCommandType type;
	union {
		struct {
			char *str;
		} msg;
		struct {
			char *msg;
		} quit;
		struct {
			char *channel;
		} join;
	};
};

char *getWord (char **str) {
	char *result = NULL;
	char *s = *str;

	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
		++s;
	}
	if (*s != 0) {
		result = s;
	}

	int len = 0;
	if (result) {
		while (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' && *s != 0) {
			++len;
			++s;
		}
		// *s = 0;
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
		char *cmdStr = getWord(&s);

		if (gjStrcmp(cmdStr, "quit") == 0) {
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
		}

		printf("command: %s \n\n", cmdStr);
	} else {
		printf("msg \n\n");
		command.type = COMMAND_MSG;
		command.msg.str = input; // todo: maybe move this into stack memory
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
		if (command.type == COMMAND_MSG) {
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
		}
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
}