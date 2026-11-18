#ifndef HOSPITAL_NET_H
#define HOSPITAL_NET_H

#ifndef FD_SETSIZE
#define FD_SETSIZE 128
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include "protocol.h"

static inline int use_exe_directory(void)
{
	char path[MAX_PATH];
	DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
	if (!length || length >= MAX_PATH) return 0;
	char *slash = strrchr(path, '\\');
	if (!slash) return 0;
	*slash = '\0';
	return _chdir(path) == 0;
}

static inline int network_address(const char *ip, const char *port, struct sockaddr_in *addr)
{
	if (!*port || strspn(port, "0123456789") != strlen(port)) return 0;
	long number = strtol(port, NULL, 10);
	if (number < 1 || number > 65535) return 0;
	memset(addr, 0, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_port = htons((unsigned short)number);
	return InetPtonA(AF_INET, ip, &addr->sin_addr) == 1;
}

static inline int start_winsock(void)
{
	WSADATA wsa;
	return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
}

#endif
