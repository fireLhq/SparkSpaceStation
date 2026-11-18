#ifndef HOSPITAL_CLIENT_COMMON_H
#define HOSPITAL_CLIENT_COMMON_H

#include "../../shared/net.h"
#include "../../shared/hospital_input.h"
#include <time.h>
#include <limits.h>

#define FILE_RULES "../common/resource/rules.txt"
#define MAX_DOCTORS MAX_WIRE_ROWS
#define PYTHON_AI_SCRIPT "python ..\\local_ai\\ai_assistant.py"

typedef WireDepartment Department;
typedef WireDoctor Doctor;
typedef WireAppointment Appointment;
typedef WirePayment Payment;

static SOCKET connection = INVALID_SOCKET;
static Department g_depts[MAX_WIRE_ROWS];
static Doctor g_doctors[MAX_DOCTORS];
static int g_dept_count, g_doctor_count;
static WireAccount current_account;

static inline void client_close(void)
{
	if (connection != INVALID_SOCKET)
	{
		closesocket(connection);
		connection = INVALID_SOCKET;
		WSACleanup();
	}
}

static inline int client_connect(int argc, char **argv)
{
	const char *ip = argc > 1 ? argv[1] : DEFAULT_IP;
	const char *port = argc > 2 ? argv[2] : DEFAULT_PORT;
	struct sockaddr_in addr;
	if (argc > 3 || !network_address(ip, port, &addr))
	{
		fputs("Usage: client.exe [server-ipv4] [port]\n", stderr);
		return 0;
	}
	if (!use_exe_directory() || !start_winsock()) return 0;
	connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	u_long mode = 1;
	if (connection == INVALID_SOCKET || ioctlsocket(connection, FIONBIO, &mode) != 0) goto failed;
	if (connect(connection, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK) goto failed;
		fd_set writable, errors;
		FD_ZERO(&writable);
		FD_SET(connection, &writable);
		errors = writable;
		struct timeval timeout = {5, 0};
		if (select(0, NULL, &writable, &errors, &timeout) <= 0 ||
		        !FD_ISSET(connection, &writable) || FD_ISSET(connection, &errors))
			goto failed;
	}
	mode = 0;
	if (ioctlsocket(connection, FIONBIO, &mode) != 0) goto failed;
	{
		DWORD timeout = 30000;
		if (setsockopt(connection, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) != 0 ||
		        setsockopt(connection, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout)) != 0)
			goto failed;
	}

	printf("Connected to %s:%s\n", ip, port);
	fflush(stdout);
	atexit(client_close);
	return 1;

failed:
	fprintf(stderr, "Cannot connect to %s:%s. Check the server and its address.\n", ip, port);
	if (connection != INVALID_SOCKET) closesocket(connection);
	connection = INVALID_SOCKET;
	WSACleanup();
	return 0;
}

static inline void connection_lost(void)
{
	HWND console = GetConsoleWindow();
	if (console) ShowWindow(console, SW_SHOW);
	fputs("Server connection lost or invalid response. Reopen the client to check the latest records.\n", stderr);
	exit(EXIT_FAILURE);
}

static inline void send_request(const WireRequest *req)
{
	const char *data = (const char *)req;
	size_t length = sizeof(*req);
	while (length)
	{
		int sent = send(connection, data, (int)length, 0);
		if (sent <= 0) connection_lost();
		data += sent;
		length -= (size_t)sent;
	}
}

static inline void receive_bytes(void *out, size_t length)
{
	char *data = (char *)out;
	while (length)
	{
		int n = recv(connection, data, (int)length, 0);
		if (n <= 0) connection_lost();
		data += n;
		length -= (size_t)n;
	}
}

static inline WireHeader exchange(WireRequest *req, void *rows, size_t stride, unsigned int capacity)
{
	req->magic = WIRE_MAGIC;
	send_request(req);
	WireHeader head;
	receive_bytes(&head, sizeof(head));
	if (head.magic != WIRE_MAGIC || head.status > RC_BUSY || head.rows > capacity ||
	        head.bytes > MAX_PAYLOAD || head.bytes != stride * head.rows ||
	        (head.status != RC_OK && (head.rows || head.next != -1)))
		connection_lost();
	if (head.bytes) receive_bytes(rows, head.bytes);
	return head;
}

static inline WireRequest request_for(uint32_t op)
{
	WireRequest req;
	memset(&req, 0, sizeof(req));
	req.op = op;
	return req;
}

static inline int rpc_action(uint32_t op, int64_t id, int64_t visit, int a, int b)
{
	WireRequest req = request_for(op);
	req.id = id;
	req.visit = visit;
	req.a = a;
	req.b = b;
	return exchange(&req, NULL, 0, 0).status == RC_OK;
}

static inline unsigned int query(uint32_t op, int a, void *rows, size_t stride, unsigned int max)
{
	WireRequest req = request_for(op);
	req.a = a;
	return exchange(&req, rows, stride, max).rows;
}

static inline void fetch_catalog(void)
{
	g_dept_count = (int)query(OP_DEPARTMENTS, 0, g_depts, sizeof(*g_depts), MAX_WIRE_ROWS);
	g_doctor_count = (int)query(OP_DOCTORS, 0, g_doctors, sizeof(*g_doctors), MAX_DOCTORS);
}

static inline void client_logout(void)
{
	rpc_action(OP_LOGOUT, 0, 0, 0, 0);
	memset(&current_account, 0, sizeof(current_account));
}

static inline int find_doctor_index(int id)
{
	for (int i = 0; i < g_doctor_count; i++)
		if (g_doctors[i].id == id) return i;
	return -1;
}

static inline int fetch_days(int idx, WireDay *days)
{
	return query(OP_DAYS, g_doctors[idx].id, days, sizeof(*days), 7) == 7;
}

static inline int fetch_pending(Appointment *app)
{
	return query(OP_PENDING, 0, app, sizeof(*app), 1) == 1;
}

static inline int book(int idx, int64_t visit, int period, Appointment *app)
{
	WireRequest req = request_for(OP_BOOK);
	req.a = g_doctors[idx].id;
	req.b = period;
	req.visit = visit;
	return exchange(&req, app, sizeof(*app), 1).rows == 1;
}

static inline int save_capacity(int idx, int am, int pm)
{
	if (!rpc_action(OP_CAPACITY, 0, 0, am, pm)) return 0;
	g_doctors[idx].max_am = am;
	g_doctors[idx].max_pm = pm;
	return 1;
}

/* Only the logged-in patient's own records are sent over the network. */
static inline void *fetch_history(uint32_t op, size_t stride, int *count)
{
	unsigned char page[PAGE_ROWS * sizeof(WireAppointment)];
	unsigned char *list = NULL;
	int offset = 0;
	*count = 0;
	for (;;)
	{
		WireRequest req = request_for(op);
		req.a = offset;
		WireHeader head = exchange(&req, page, stride, PAGE_ROWS);
		if (head.status != RC_OK) break;
		if (head.rows)
		{
			size_t total = (size_t)*count + head.rows;
			if (total > INT_MAX || total > SIZE_MAX / stride) connection_lost();
			unsigned char *grown = (unsigned char *)realloc(list, total * stride);
			if (!grown) connection_lost();
			list = grown;
			memcpy(list + (size_t)*count * stride, page, head.bytes);
			*count = (int)total;
		}
		if (head.next == -1) break;
		if (head.next <= offset || !head.rows) connection_lost();
		offset = head.next;
	}
	return list;
}

static inline struct tm *display_time(int64_t timestamp)
{
	time_t value = (time_t)timestamp;
	return localtime(&value);
}

#endif
