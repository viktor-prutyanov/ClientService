#include "stdafx.h"

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#define BUF_SIZE 4096
#define EXIT_CMD "exit"

HANDLE StdinHandle;
bool Connection_lost = false;

DWORD WINAPI receive_loop_func(LPVOID lpParam);

int send_loop_func(SOCKET sock)
{
	int err = 0;
	CHAR buf[BUF_SIZE];
	DWORD dwRead;
    BOOL bSuccess;

	StdinHandle = GetStdHandle(STD_INPUT_HANDLE);
    if (StdinHandle == INVALID_HANDLE_VALUE)
        return -1;

	for (;;) {
        bSuccess = ReadFile(StdinHandle, buf, BUF_SIZE, &dwRead, NULL);

        if (Connection_lost || !bSuccess) {
			if (!bSuccess)
				printf("Failed read from stdin fwith error %d\n", GetLastError());
			err = -1;
			break;
		}

		if (strncmp(buf, EXIT_CMD, strlen(EXIT_CMD)) == 0) {
			printf("Exiting...\n");
			err = 1;
			break;
		}

		err = send(sock, buf, (int)strlen(buf), 0);
		if (err == SOCKET_ERROR) {
            err = -1;
            break;
		}		
		
        ZeroMemory(&buf, BUF_SIZE);
	}

	CloseHandle(StdinHandle);
	return err;
}

int do_connect(SOCKET *psock, PCSTR ip, PCSTR port) {
	struct addrinfo *ai = NULL, hints;
    int err = 0;
    SOCKET sock;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

    if (err = getaddrinfo(ip, port, &hints, &ai)) {
        fprintf(stderr, "Failed to get address info\n");
        goto out_err;
    }
		
	sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock == INVALID_SOCKET) {
        fprintf(stderr, "Failed to create socket\n");
        err = -1;
        goto out_ai;
    }

	if (SOCKET_ERROR == (err = connect(sock, ai->ai_addr, (int)ai->ai_addrlen))) {
        fprintf(stderr, "Failed to connect\n");
        goto out_socket;
	}

    *psock = sock;
    return 0;

out_socket:
    closesocket(sock);
out_ai:
	freeaddrinfo(ai);
out_err:
	return err;
}

DWORD WINAPI receive_loop_func(LPVOID lpParam)
{
	SOCKET sock = (SOCKET)lpParam;
	char buf[BUF_SIZE];
	int buf_size = BUF_SIZE;
    int err;

	for (;;) {
		err = recv(sock, buf, buf_size, 0);

		if (err > 0) {
			printf("%s", buf);
		    ZeroMemory(buf, BUF_SIZE);
		} else if (err == 0) {
			fprintf(stderr, "Connection closed\n");
			break;
		} else {
			fprintf(stderr, "Failed to receive with error %d\n", WSAGetLastError());
			Connection_lost = true;
			CancelIoEx(StdinHandle, NULL);
			CloseHandle(StdinHandle);
			StdinHandle = INVALID_HANDLE_VALUE;
			break;
		}
	}

	return 0;
}

int recv_init_msg(SOCKET socket)
{
	char buf[BUF_SIZE];
	int buf_size = BUF_SIZE;
    int recv_size = 0;
	
    ZeroMemory(&buf, buf_size);

	recv_size = recv(socket, buf, buf_size, 0);
    if (recv_size > 0) {
        printf("%s", buf);
    }

	return recv_size;
}

int _tmain(int argc, char *argv[])
{
    const char usage[] = "usage:\n"
                         "\tclient.exe ip port\n";
    char *ip, *port;
	SOCKET sock = INVALID_SOCKET;
    int err = 0;
    WSADATA wsaData;
	DWORD exitCode;
	HANDLE hThread;
	DWORD dwThreadId;

    if (argc != 3) {
        fprintf(stderr, "%s", usage);
        return -1;
    }

    ip = argv[1];
    port = argv[2];
    
    if (err = WSAStartup(MAKEWORD(2, 2), &wsaData))
    {
        fprintf(stderr, "WSAStartup failed");
        return err;
    }

    if (err = do_connect(&sock, ip, port)) {
        goto out_err;
	}

	if ((err = recv_init_msg(sock)) <= 0) {
        goto out_conn;
	}

	hThread = CreateThread(NULL, 0, receive_loop_func, (LPVOID)sock, 0,	&dwThreadId);

    if ((err = send_loop_func(sock)) <= 0) {
        goto out_thread;
	}

    fprintf(stderr, "Client is closing...\n");

out_thread:
    GetExitCodeThread(hThread, &exitCode);
    TerminateThread(hThread, exitCode);
out_conn:
    shutdown(sock, SD_SEND);
    closesocket(sock);
out_err:
    WSACleanup();
    return err;
}
