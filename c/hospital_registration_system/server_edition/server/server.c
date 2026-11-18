#include "../shared/net.h"
#include "../shared/hospital_input.h"
#include "hospital_core.h"

#define MAX_CLIENTS 64

typedef struct
{
	SOCKET socket;
	unsigned long number;
	char ip[INET_ADDRSTRLEN];
	int role;
	int account;
	long long called;
	ULONGLONG last_input;
	WireRequest request;
	size_t received;
	WireHeader header;
	unsigned char output[sizeof(WireHeader) + MAX_PAYLOAD];
	size_t sent, length;
} Client;

static Client clients[MAX_CLIENTS];

static const char *op_name(uint32_t op)
{
	const char *names[] = {"?", "LOGIN", "REGISTER", "LOGOUT", "AI", "DEPARTMENTS", "DOCTORS",
	                      "DAYS", "PENDING", "BOOK", "PAYMENT", "APPOINTMENTS", "RESCHEDULE",
	                      "PAYMENTS", "QUEUE", "CALL", "FINISH", "CAPACITY"};
	return op < sizeof(names) / sizeof(*names) ? names[op] : "?";
}

static void add_row(Client *client, const void *row, size_t length)
{
	memcpy(client->output + sizeof(WireHeader) + client->header.bytes, row, length);
	client->header.bytes += (uint32_t)length;
	client->header.rows++;
}

static WireAppointment appointment_info(Appointment app)
{
	double remaining = UNPAID_TIMEOUT_SECONDS - difftime(time(NULL), app.created);
	WireAppointment row = {app.id, (int32_t)app.patient_id, app.doctor_id, app.visit, app.created,
	                       app.period, app.status, is_appointment_paid(app.id),
	                       app.status == 0 && remaining > 0 ?
	                       (remaining > UNPAID_TIMEOUT_SECONDS ? UNPAID_TIMEOUT_SECONDS : (int)remaining) : 0};
	return row;
}

static void add_appointment(Client *client, Appointment app)
{
	WireAppointment row = appointment_info(app);
	add_row(client, &row, sizeof(row));
}

static AppointmentNode *called_node(Client *client, int idx)
{
	if (idx < 0 || !client->called) return NULL;
	for (AppointmentNode *node = g_doctors[idx].head; node; node = node->next)
		if (node->data.id == client->called && node->data.status == 2) return node;
	return NULL;
}

static void end_session(Client *client)
{
	if (client->role == ROLE_PATIENT)
		cleanup_patient_unpaid_on_exit(client->account);
	else if (client->role == ROLE_DOCTOR)
	{
		int idx = find_doctor_index(client->account);
		AppointmentNode *node = called_node(client, idx);
		if (node)
		{
			int expired = is_expired_at(node->data.visit, node->data.period,
			                            &g_doctors[idx].info, time(NULL));
			change_status(idx, node, expired ? 3 : 1);
		}
	}
	client->called = 0;
	client->role = ROLE_NONE;
	client->account = 0;
}

static int token_valid(const char *text, size_t size)
{
	if (!memchr(text, 0, size) || !*text) return 0;
	for (; *text; text++)
	{
		unsigned char ch = (unsigned char)*text;
		if (ch <= 32 || ch == 127) return 0;
	}
	return 1;
}

static int login(Client *client, const WireRequest *req)
{
	if (client->role || (req->a != ROLE_PATIENT && req->a != ROLE_DOCTOR) ||
	        req->id < 1 || req->id > INT_MAX ||
	        !token_valid(req->password, sizeof(req->password)))
		return RC_BAD;

	WireAccount row = {0};
	row.id = (int32_t)req->id;
	row.role = req->a;
	if (req->a == ROLE_PATIENT)
	{
		Patient patient;
		if (!find_patient_by_id(row.id, &patient) ||
		        !verify_password(req->password, patient.password)) return RC_AUTH;
		strcpy(row.name, patient.name);
	}
	else
	{
		int idx = find_doctor_index(row.id);
		if (idx < 0 || !verify_password(req->password, g_doctors[idx].info.password)) return RC_AUTH;
		strcpy(row.name, g_doctors[idx].info.name);
	}
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (clients[i].role == row.role && clients[i].account == row.id) return RC_BUSY;

	client->role = row.role;
	client->account = row.id;
	add_row(client, &row, sizeof(row));
	return RC_OK;
}

static int register_patient(Client *client, const WireRequest *req)
{
	if (client->role || req->id < 100000001 || req->id > 999999999 ||
	        req->a < 0 || req->a > 1 || req->b < 19000101 || req->b > 99991231 ||
	        !token_valid(req->name, sizeof(req->name)) ||
	        !token_valid(req->phone, sizeof(req->phone)) ||
	        !token_valid(req->password, sizeof(req->password)) ||
	        !valid_password_input(req->password) ||
	        strspn(req->phone, "0123456789") != strlen(req->phone))
		return RC_BAD;
	if (find_patient_by_id((long)req->id, NULL)) return RC_BUSY;
	Patient patient = {0};
	char birth[16];
	int date = req->b;
	snprintf(birth, sizeof(birth), "%04d-%02d-%02d", date / 10000, date / 100 % 100, date % 100);
	if (!parse_birth(birth, &patient.birth)) return RC_BAD;

	patient.id = (long)req->id;
	patient.gender = req->a;
	strcpy(patient.name, req->name);
	strcpy(patient.phone, req->phone);
	if (!hash_password(req->password, patient.password)) return RC_UNAVAILABLE;
	save_patient_sorted(patient);
	return RC_OK;
}

static void list_catalog(Client *client, uint32_t op)
{
	if (op == OP_DEPARTMENTS)
	{
		for (int i = 0; i < g_dept_count; i++)
		{
			Department *dept = &g_depts[i];
			WireDepartment row = {0};
			row.id = dept->id;
			strcpy(row.name, dept->name);
			strcpy(row.location, dept->location);
			strcpy(row.description, dept->description);
			add_row(client, &row, sizeof(row));
		}
	}
	else
	{
		for (int i = 0; i < g_doctor_count; i++)
		{
			Doctor *doc = &g_doctors[i].info;
			WireDoctor row = {0};
			row.id = doc->id;
			row.department_id = doc->department_id;
			row.gender = doc->gender;
			strcpy(row.name, doc->name);
			strcpy(row.title, doc->title);
			row.fee = doc->fee;
			row.start_work_hour = doc->start_work_hour;
			row.end_work_hour = doc->end_work_hour;
			row.max_am = doc->max_am;
			row.max_pm = doc->max_pm;
			add_row(client, &row, sizeof(row));
		}
	}
}

static int list_history(Client *client, const WireRequest *req)
{
	int limit = req->op == OP_APPOINTMENTS ? g_app_count : g_payment_count;
	if (req->a < 0 || req->a > limit) return RC_BAD;
	int i = req->a;
	for (; i < limit && client->header.rows < PAGE_ROWS; i++)
	{
		if (req->op == OP_APPOINTMENTS)
		{
			if (g_apps[i].patient_id == client->account) add_appointment(client, g_apps[i]);
		}
		else
		{
			Payment *pay = &g_payments[i];
			Appointment *app = find_appointment(pay->appointment_id);
			if (app && app->patient_id == client->account)
			{
				WirePayment row = {pay->id, pay->appointment_id, pay->paid, pay->amount};
				add_row(client, &row, sizeof(row));
			}
		}
	}
	if (i < limit) client->header.next = i;
	return RC_OK;
}

static void list_queue(Client *client, time_t now)
{
	for (int i = 0; i < g_doctor_count; i++)
	{
		Doctor *doc = &g_doctors[i].info;
		int ahead = 0;
		for (AppointmentNode *node = g_doctors[i].head; node; node = node->next)
		{
			Appointment *app = &node->data;
			if (app->status != 1 || !is_consultation_time(app, doc, now)) continue;
			if (app->patient_id == client->account)
			{
				WireQueue row = {0};
				strcpy(row.doc_name, doc->name);
				for (int j = 0; j < g_dept_count; j++)
				{
					if (g_depts[j].id != doc->department_id) continue;
					strcpy(row.dept_name, g_depts[j].name);
					strcpy(row.dept_location, g_depts[j].location);
					break;
				}
				row.end_hour = doc->end_work_hour;
				row.ahead_count = ahead;
				row.period = app->period;
				add_row(client, &row, sizeof(row));
				break;
			}
			ahead++;
		}
	}
}

static int handle_patient(Client *client, const WireRequest *req)
{
	time_t now = time(NULL);
	int idx;
	AppointmentNode *node;
	switch (req->op)
	{
		case OP_AI:
			return RC_OK;
		case OP_DAYS:
			idx = find_doctor_index(req->a);
			if (idx < 0) return RC_BAD;
			for (int i = 0; i < 7; i++)
			{
				time_t date = day_zero(now, i);
				struct tm *day = localtime(&date);
				if (!day) return RC_UNAVAILABLE;
				WireDay row = {date, day->tm_year + 1900, day->tm_mon + 1, day->tm_mday,
				               remaining_slots(idx, date, 0), remaining_slots(idx, date, 1),
				               slot_available(idx, date, 0, now), slot_available(idx, date, 1, now)};
				add_row(client, &row, sizeof(row));
			}
			return RC_OK;
		case OP_PENDING:
			node = find_unpaid_appointment_in_memory(client->account, &idx);
			if (!node) return RC_EMPTY;
			add_appointment(client, node->data);
			return RC_OK;
		case OP_BOOK:
		case OP_RESCHEDULE:
		{
			int day = 0;
			while (day < 7 && req->visit != day_zero(now, day)) day++;
			if (day == 7 || (req->b != 0 && req->b != 1)) return RC_UNAVAILABLE;
			if (req->op == OP_BOOK)
			{
				idx = find_doctor_index(req->a);
				if (idx < 0) return RC_BAD;
				node = create_appointment(client->account, idx, (time_t)req->visit, req->b);
				if (!node) return RC_UNAVAILABLE;
				add_appointment(client, node->data);
				return RC_OK;
			}
			Appointment *app = find_appointment(req->id);
			if (!app || app->patient_id != client->account) return RC_UNAVAILABLE;
			idx = find_doctor_index(app->doctor_id);
			return idx >= 0 &&
			       reschedule_appointment(client->account, *app, idx, (time_t)req->visit, req->b)
			       ? RC_OK : RC_UNAVAILABLE;
		}
		case OP_PAYMENT:
			if (req->a != 1 && req->a != 2) return RC_BAD;
			node = find_unpaid_appointment_in_memory(client->account, &idx);
			if (!node || node->data.id != req->id) return RC_UNAVAILABLE;
			if (req->a == 1) return pay_appointment(node, idx) ? RC_OK : RC_UNAVAILABLE;
			change_status(idx, node, 4);
			return RC_OK;
		case OP_APPOINTMENTS:
		case OP_PAYMENTS:
			return list_history(client, req);
		case OP_QUEUE:
			list_queue(client, now);
			return RC_OK;
	}
	return RC_AUTH;
}

static int handle_doctor(Client *client, const WireRequest *req)
{
	int idx = find_doctor_index(client->account);
	if (idx < 0) return RC_AUTH;
	Doctor *doc = &g_doctors[idx].info;
	time_t now = time(NULL);
	AppointmentNode *node = called_node(client, idx);
	switch (req->op)
	{
		case OP_CAPACITY:
			if (req->a < 0 || req->a > 99 || req->b < 0 || req->b > 99) return RC_BAD;
			return save_capacity(idx, req->a, req->b) ? RC_OK : RC_UNAVAILABLE;
		case OP_CALL:
		{
			if (node) return RC_BUSY;
			for (node = g_doctors[idx].head; node; node = node->next)
				if (node->data.status == 1 && is_consultation_time(&node->data, doc, now)) break;
			if (!node) return RC_EMPTY;
			Patient patient;
			WireCall row = {0};
			row.id = node->data.id;
			row.patient_id = (int32_t)node->data.patient_id;
			row.gender = row.age = -1;
			if (find_patient_by_id(row.patient_id, &patient))
			{
				strcpy(row.name, patient.name);
				row.gender = patient.gender;
				row.age = calculate_age(patient.birth);
			}
			change_status(idx, node, 2);
			client->called = row.id;
			add_row(client, &row, sizeof(row));
			return RC_OK;
		}
		case OP_FINISH:
			if (req->a < 0 || req->a > 2) return RC_BAD;
			if (!node || req->id != client->called) return RC_UNAVAILABLE;
			change_status(idx, node, req->a == 1 ? 3 : req->a == 2 ? 4 : 1);
			client->called = 0;
			return RC_OK;
	}
	return RC_AUTH;
}

static int dispatch(Client *client)
{
	const WireRequest *req = &client->request;
	switch (req->op)
	{
		case OP_LOGIN: return login(client, req);
		case OP_REGISTER: return register_patient(client, req);
		case OP_LOGOUT:
			end_session(client);
			return RC_OK;
	}
	if (!client->role) return RC_AUTH;
	if (req->op == OP_DEPARTMENTS || req->op == OP_DOCTORS)
	{
		list_catalog(client, req->op);
		return RC_OK;
	}
	clean_all_expired_appointments();
	return client->role == ROLE_PATIENT ? handle_patient(client, req) : handle_doctor(client, req);
}

static void respond(Client *client)
{
	WireHeader header = {WIRE_MAGIC, RC_OK, 0, 0, -1};
	client->header = header;
	client->header.status = dispatch(client);
	if (client->header.status != RC_OK)
	{
		client->header.rows = client->header.bytes = 0;
		client->header.next = -1;
	}
	time_t now = time(NULL);
	char stamp[24];
	strftime(stamp, sizeof(stamp), "%H:%M:%S", localtime(&now));
	printf("[%s] %s #%lu %s result=%u rows=%u\n", stamp, client->ip, client->number,
	       op_name(client->request.op), client->header.status, client->header.rows);

	memcpy(client->output, &client->header, sizeof(WireHeader));
	client->length = sizeof(WireHeader) + client->header.bytes;
	client->sent = client->received = 0;
	SecureZeroMemory(&client->request, sizeof(client->request));
}

static void disconnect_client(Client *client)
{
	printf("Disconnected %s #%lu\n", client->ip, client->number);
	end_session(client);
	closesocket(client->socket);
	SecureZeroMemory(client, sizeof(*client));
	client->socket = INVALID_SOCKET;
}

static void accept_client(SOCKET listener)
{
	static unsigned long connection_number;
	struct sockaddr_in peer;
	int length = sizeof(peer);
	SOCKET socket = accept(listener, (struct sockaddr *)&peer, &length);
	if (socket == INVALID_SOCKET) return;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		Client *client = &clients[i];
		if (client->socket != INVALID_SOCKET) continue;
		u_long mode = 1;
		BOOL keepalive = TRUE;
		if (ioctlsocket(socket, FIONBIO, &mode) == SOCKET_ERROR ||
		        setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, (const char *)&keepalive,
		                   sizeof(keepalive)) == SOCKET_ERROR) break;
		client->socket = socket;
		client->number = ++connection_number;
		client->last_input = GetTickCount64();
		InetNtopA(AF_INET, &peer.sin_addr, client->ip, sizeof(client->ip));
		printf("Connected %s #%lu\n", client->ip, client->number);
		return;
	}
	closesocket(socket);
}

static void run_server(SOCKET listener)
{
	for (int i = 0; i < MAX_CLIENTS; i++) clients[i].socket = INVALID_SOCKET;
	time_t last_cleanup = 0;
	for (;;)
	{
		fd_set readable, writable;
		FD_ZERO(&readable);
		FD_ZERO(&writable);
		FD_SET(listener, &readable);
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			Client *client = &clients[i];
			if (client->socket == INVALID_SOCKET) continue;
			if (client->length) FD_SET(client->socket, &writable);
			else FD_SET(client->socket, &readable);
		}

		struct timeval wait = {1, 0};
		if (select(0, &readable, &writable, NULL, &wait) == SOCKET_ERROR)
		{
			fprintf(stderr, "select failed: %d\n", WSAGetLastError());
			break;
		}
		time_t now = time(NULL);
		if (now != last_cleanup)
		{
			clean_all_expired_appointments();
			last_cleanup = now;
		}
		if (FD_ISSET(listener, &readable)) accept_client(listener);
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			Client *client = &clients[i];
			SOCKET socket = client->socket;
			if (socket == INVALID_SOCKET) continue;
			ULONGLONG idle = GetTickCount64() - client->last_input;
			if ((client->received || client->length) ? idle >= 30000 :
			        idle >= (client->role ? 3600000 : 120000))
			{
				disconnect_client(client);
				continue;
			}

			int moved;
			if (FD_ISSET(socket, &readable))
			{
				char *dest = (char *)&client->request + client->received;
				moved = recv(socket, dest, (int)(sizeof(client->request) - client->received), 0);
				if (moved > 0)
				{
					client->last_input = GetTickCount64();
					client->received += (size_t)moved;
					if (client->received == sizeof(client->request))
					{
						if (client->request.magic != WIRE_MAGIC) disconnect_client(client);
						else respond(client);
					}
					continue;
				}
			}
			else if (FD_ISSET(socket, &writable))
			{
				moved = send(socket, (const char *)client->output + client->sent,
				             (int)(client->length - client->sent), 0);
				if (moved > 0)
				{
					client->sent += (size_t)moved;
					if (client->sent == client->length) client->length = client->sent = 0;
					continue;
				}
			}
			else continue;
			if (moved == 0 || WSAGetLastError() != WSAEWOULDBLOCK) disconnect_client(client);
		}
	}
}

int main(int argc, char **argv)
{
	const char *ip = argc > 1 ? argv[1] : DEFAULT_IP;
	const char *port = argc > 2 ? argv[2] : DEFAULT_PORT;
	struct sockaddr_in addr;
	if (argc > 3 || !network_address(ip, port, &addr))
	{
		fputs("Usage: hospital_server.exe [bind-ipv4] [port]\n", stderr);
		return 1;
	}

	setvbuf(stdout, NULL, _IONBF, 0);
	if (!use_exe_directory() || !start_winsock())
	{
		fputs("Cannot open executable directory or initialize networking.\n", stderr);
		return 1;
	}
	SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	u_long mode = 1;
	BOOL exclusive = TRUE;
	if (listener == INVALID_SOCKET ||
	        setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive,
	                   sizeof(exclusive)) != 0 ||
	        ioctlsocket(listener, FIONBIO, &mode) != 0 ||
	        bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
	        listen(listener, SOMAXCONN) != 0)
	{
		fprintf(stderr, "Cannot listen at %s:%s (Winsock %d).\n", ip, port, WSAGetLastError());
		return 1;
	}
	init_system();
	printf("Hospital server listening on %s:%s (departments=%d, doctors=%d).\n",
	       ip, port, g_dept_count, g_doctor_count);
	run_server(listener);
	closesocket(listener);
	WSACleanup();
	return 1;
}

