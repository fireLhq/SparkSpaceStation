#ifndef HOSPITAL_CORE_H
#define HOSPITAL_CORE_H

#include "data_store.h"

#define UNPAID_TIMEOUT_SECONDS 600

typedef struct AppointmentNode
{
	Appointment data;
	struct AppointmentNode *next;
} AppointmentNode;

typedef struct
{
	Doctor info;
	AppointmentNode *head;
} DoctorListHeader;

static DoctorListHeader g_doctors[MAX_DOCTORS];
static int g_doctor_count;
static Department *g_depts;
static int g_dept_count;
static Patient *g_patients;
static int g_patient_count;
static Appointment *g_apps;
static int g_app_count;
static Payment *g_payments;
static int g_payment_count;
static long long g_last_app_id;
static long long g_last_payment_id;

static void data_error(const char *path)
{
	HWND console = GetConsoleWindow();
	if (console) ShowWindow(console, SW_SHOW);
	fprintf(stderr, "Database error: %s. Close other copies or check the data folder, then restart.\n", path);
	exit(EXIT_FAILURE);
}

static void *table_or_exit(int table, int *count, int *legacy)
{
	void *records;
	if (!load_table(table, &records, count, legacy)) data_error(table_paths[table]);
	return records;
}

static void save_or_exit(int table, void *records, int count)
{
	if (!save_table(table, records, count)) data_error(table_paths[table]);
}

static void *grow_records(void *records, int count, size_t size)
{
	if (count == INT_MAX || (size_t)count + 1 > SIZE_MAX / size) data_error("record limit");
	void *out = realloc(records, ((size_t)count + 1) * size);
	if (!out) data_error("out of memory");
	return out;
}

static int find_doctor_index(int doctor_id)
{
	int low = 0, high = g_doctor_count - 1;
	while (low <= high)
	{
		int mid = low + (high - low) / 2;
		if (g_doctors[mid].info.id == doctor_id) return mid;
		if (g_doctors[mid].info.id < doctor_id) low = mid + 1;
		else high = mid - 1;
	}
	return -1;
}

static int patient_position(long id)
{
	int low = 0, high = g_patient_count;
	while (low < high)
	{
		int mid = low + (high - low) / 2;
		if (g_patients[mid].id < id) low = mid + 1;
		else high = mid;
	}
	return low;
}

static int find_patient_by_id(long id, Patient *out)
{
	int pos = patient_position(id);
	if (pos == g_patient_count || g_patients[pos].id != id) return 0;
	if (out) *out = g_patients[pos];
	return 1;
}

static void save_patient_sorted(Patient patient)
{
	int pos = patient_position(patient.id);
	g_patients = (Patient *)grow_records(g_patients, g_patient_count, sizeof(Patient));
	memmove(g_patients + pos + 1, g_patients + pos,
	        (size_t)(g_patient_count - pos) * sizeof(Patient));
	g_patients[pos] = patient;
	save_or_exit(PATIENTS, g_patients, ++g_patient_count);
}

static Appointment *find_appointment(long long id)
{
	for (int i = 0; i < g_app_count; i++)
		if (g_apps[i].id == id) return &g_apps[i];
	return NULL;
}

static Payment *find_payment(long long appointment_id)
{
	for (int i = 0; i < g_payment_count; i++)
		if (g_payments[i].appointment_id == appointment_id) return &g_payments[i];
	return NULL;
}

static inline int is_appointment_paid(long long id)
{
	return find_payment(id) != NULL;
}

static void update_appointment_in_file(Appointment app)
{
	Appointment *old = find_appointment(app.id);
	if (old) *old = app;
	else
	{
		g_apps = (Appointment *)grow_records(g_apps, g_app_count, sizeof(Appointment));
		g_apps[g_app_count++] = app;
	}
	save_or_exit(APPOINTMENTS, g_apps, g_app_count);
}

static void apply_payment(PaymentTransaction tx)
{
	Appointment *old = find_appointment(tx.app.id);
	Payment *paid = find_payment(tx.app.id);
	if (!old || old->patient_id != tx.app.patient_id || old->doctor_id != tx.app.doctor_id ||
	        old->visit != tx.app.visit || old->period != tx.app.period ||
	        old->created != tx.app.created || (old->status != 0 && old->status != 1))
		data_error(FILE_TRANSACTION);
	if (paid && (paid->id != tx.pay.id || paid->paid != tx.pay.paid || paid->amount != tx.pay.amount))
		data_error(FILE_TRANSACTION);
	if (!paid)
	{
		for (int i = 0; i < g_payment_count; i++)
			if (g_payments[i].id == tx.pay.id) data_error(FILE_TRANSACTION);
		g_payments = (Payment *)grow_records(g_payments, g_payment_count, sizeof(Payment));
		g_payments[g_payment_count++] = tx.pay;
		save_or_exit(PAYMENTS, g_payments, g_payment_count);
	}
	if (old->status != 1) update_appointment_in_file(tx.app);
	save_or_exit(TRANSACTION, NULL, 0);
}

static time_t day_zero(time_t now, int offset)
{
	struct tm *ptr = localtime(&now);
	if (!ptr) data_error("local clock");
	struct tm day = *ptr;
	day.tm_hour = day.tm_min = day.tm_sec = 0;
	day.tm_mday += offset;
	day.tm_isdst = -1;
	return mktime(&day);
}

static int period_supported(const Doctor *doc, int period)
{
	return period == 0 ? doc->start_work_hour < 12 : doc->end_work_hour > 12;
}

static int is_expired_at(time_t visit, int period, const Doctor *doc, time_t now)
{
	time_t today = day_zero(now, 0);
	if (visit < today || !period_supported(doc, period)) return 1;
	if (visit > today) return 0;
	int hour = localtime(&now)->tm_hour;
	int end = period == 0 && doc->end_work_hour > 12 ? 12 : doc->end_work_hour;
	return hour >= end;
}

static int is_consultation_time(const Appointment *app, const Doctor *doc, time_t now)
{
	if (app->visit != day_zero(now, 0)) return 0;
	int hour = localtime(&now)->tm_hour;
	return app->period == (hour < 12 ? 0 : 1) && hour >= doc->start_work_hour &&
	       hour < doc->end_work_hour;
}

static int appointment_less(Appointment a, Appointment b)
{
	if (a.visit != b.visit) return a.visit < b.visit;
	if (a.period != b.period) return a.period < b.period;
	return a.created < b.created;
}

static void insert_appointment_to_chain(int doc_idx, Appointment app)
{
	AppointmentNode *node = (AppointmentNode *)malloc(sizeof(AppointmentNode));
	if (!node) data_error("out of memory");
	node->data = app;
	AppointmentNode **link = &g_doctors[doc_idx].head;
	while (*link && !appointment_less(app, (*link)->data)) link = &(*link)->next;
	node->next = *link;
	*link = node;
}

static void remove_appointment_node(int doc_idx, long long id)
{
	AppointmentNode **link = &g_doctors[doc_idx].head;
	while (*link && (*link)->data.id != id) link = &(*link)->next;
	if (*link)
	{
		AppointmentNode *node = *link;
		*link = node->next;
		free(node);
	}
}

static void change_status(int doc_idx, AppointmentNode *node, int status)
{
	Appointment app = node->data;
	app.status = status;
	update_appointment_in_file(app);
	node->data = app;
	if (status == 4) remove_appointment_node(doc_idx, app.id);
}

static int pending_expired(const Appointment *app, const Doctor *doc, time_t now)
{
	return difftime(now, app->created) >= UNPAID_TIMEOUT_SECONDS ||
	       is_expired_at(app->visit, app->period, doc, now);
}

static void clean_all_expired_appointments(void)
{
	time_t now = time(NULL), today = day_zero(now, 0);
	int changed = 0;
	for (int i = 0; i < g_doctor_count; i++)
	{
		AppointmentNode **link = &g_doctors[i].head;
		while (*link)
		{
			AppointmentNode *node = *link;
			Appointment *app = &node->data;
			int status = app->status;
			if (status == 0 && pending_expired(app, &g_doctors[i].info, now))
				app->status = 4;
			else if ((status == 1 || status == 2) &&
			         is_expired_at(app->visit, app->period, &g_doctors[i].info, now))
				app->status = status == 1 ? 4 : 3;
			if (app->status != status)
			{
				Appointment *record = find_appointment(app->id);
				if (!record) data_error(FILE_APPOINTMENT);
				*record = *app;
				changed = 1;
			}
			if (app->status == 4 || (app->status == 3 && app->visit < today))
			{
				*link = node->next;
				free(node);
			}
			else link = &node->next;
		}
	}
	if (changed) save_or_exit(APPOINTMENTS, g_apps, g_app_count);
}

static void init_system(void)
{
	if (!lock_database()) data_error("data/.lock");
	int legacy_dept, legacy_doc, legacy_patient, legacy_app, legacy_pay, ignored;
	g_depts = (Department *)table_or_exit(DEPARTMENTS, &g_dept_count, &legacy_dept);
	Doctor *doctors = (Doctor *)table_or_exit(DOCTORS, &g_doctor_count, &legacy_doc);
	g_patients = (Patient *)table_or_exit(PATIENTS, &g_patient_count, &legacy_patient);
	g_apps = (Appointment *)table_or_exit(APPOINTMENTS, &g_app_count, &legacy_app);
	g_payments = (Payment *)table_or_exit(PAYMENTS, &g_payment_count, &legacy_pay);
	int tx_count;
	PaymentTransaction *tx = (PaymentTransaction *)table_or_exit(TRANSACTION, &tx_count, &ignored);
	if (g_dept_count > MAX_DEPARTMENTS || g_doctor_count > MAX_DOCTORS)
		data_error("department or doctor limit");
	for (int i = 1; i < g_dept_count; i++)
		if (g_depts[i - 1].id >= g_depts[i].id) data_error(FILE_DEPARTMENT);
	for (int i = 0; i < g_doctor_count; i++)
	{
		if (i && doctors[i - 1].id >= doctors[i].id) data_error(FILE_DOCTOR);
		int j = 0;
		while (j < g_dept_count && g_depts[j].id != doctors[i].department_id) j++;
		if (j == g_dept_count) data_error(FILE_DOCTOR);
		g_doctors[i].info = doctors[i];
	}
	for (int i = 1; i < g_patient_count; i++)
		if (g_patients[i - 1].id >= g_patients[i].id) data_error(FILE_PATIENT);
	for (int i = 0; i < g_app_count; i++)
	{
		if (g_apps[i].id > g_last_app_id) g_last_app_id = g_apps[i].id;
		if (find_appointment(g_apps[i].id) != &g_apps[i]) data_error(FILE_APPOINTMENT);
		if (g_apps[i].status < 0 || g_apps[i].status > 4) g_apps[i].status = 4;
	}
	for (int i = 0; i < g_payment_count; i++)
	{
		Payment *pay = &g_payments[i];
		if (pay->id > g_last_payment_id) g_last_payment_id = pay->id;
		if (find_payment(pay->appointment_id) != pay || !find_appointment(pay->appointment_id))
			data_error(FILE_PAYMENT);
		for (int j = 0; j < i; j++)
			if (g_payments[j].id == pay->id) data_error(FILE_PAYMENT);
	}
	if (tx_count > 1) data_error(FILE_TRANSACTION);
	if (tx_count == 1)
	{
		apply_payment(*tx);
		if (tx->pay.id > g_last_payment_id) g_last_payment_id = tx->pay.id;
	}
	free(tx);

	time_t now = time(NULL), today = day_zero(now, 0);
	int app_changed = legacy_app;
	for (int i = 0; i < g_app_count; i++)
	{
		Appointment *app = &g_apps[i];
		int doc_idx = find_doctor_index(app->doctor_id);
		int old = app->status;
		if (old == 0) app->status = 4;
		else if (old == 1 || old == 2)
		{
			if (doc_idx < 0 || !find_patient_by_id(app->patient_id, NULL) ||
			        !is_appointment_paid(app->id))
				app->status = 4;
			else if (is_expired_at(app->visit, app->period, &g_doctors[doc_idx].info, now))
				app->status = old == 2 ? 3 : 4;
			else app->status = 1;
		}
		if (app->status != old) app_changed = 1;
		if (doc_idx >= 0 && app->visit >= today && (app->status == 1 || app->status == 3))
			insert_appointment_to_chain(doc_idx, *app);
	}
	if (app_changed) save_or_exit(APPOINTMENTS, g_apps, g_app_count);
	if (legacy_dept) save_or_exit(DEPARTMENTS, g_depts, g_dept_count);
	if (legacy_doc) save_or_exit(DOCTORS, doctors, g_doctor_count);
	if (legacy_patient) save_or_exit(PATIENTS, g_patients, g_patient_count);
	if (legacy_pay) save_or_exit(PAYMENTS, g_payments, g_payment_count);
	free(doctors);
}

static AppointmentNode *find_unpaid_appointment_in_memory(long id, int *doc_idx)
{
	for (int i = 0; i < g_doctor_count; i++)
	{
		for (AppointmentNode *node = g_doctors[i].head; node; node = node->next)
		{
			if (node->data.patient_id == id && node->data.status == 0)
			{
				*doc_idx = i;
				return node;
			}
		}
	}
	return NULL;
}

static void cleanup_patient_unpaid_on_exit(long id)
{
	int doc_idx;
	AppointmentNode *node;
	while ((node = find_unpaid_appointment_in_memory(id, &doc_idx)) != NULL)
		change_status(doc_idx, node, 4);
}

static int has_active_appointment(int doc_idx, long id)
{
	for (AppointmentNode *node = g_doctors[doc_idx].head; node; node = node->next)
		if (node->data.patient_id == id && node->data.status <= 2) return 1;
	return 0;
}

static int get_booked_count(int doc_idx, time_t visit, int period)
{
	int count = 0;
	for (AppointmentNode *node = g_doctors[doc_idx].head; node; node = node->next)
	{
		if (node->data.visit > visit) break;
		if (node->data.visit == visit && node->data.period == period && node->data.status <= 3)
			count++;
	}
	return count;
}

static int remaining_slots(int doc_idx, time_t visit, int period)
{
	Doctor *doc = &g_doctors[doc_idx].info;
	int limit = period ? doc->max_pm : doc->max_am;
	int booked = get_booked_count(doc_idx, visit, period);
	return booked >= limit ? 0 : limit - booked;
}

static int slot_available(int doc_idx, time_t visit, int period, time_t now)
{
	Doctor *doc = &g_doctors[doc_idx].info;
	if (period < 0 || period > 1 || visit < day_zero(now, 0) || visit > day_zero(now, 6))
		return 0;
	return !is_expired_at(visit, period, doc, now) &&
	       remaining_slots(doc_idx, visit, period) > 0;
}

static AppointmentNode *create_appointment(long patient_id, int doc_idx, time_t visit, int period)
{
	clean_all_expired_appointments();
	time_t now = time(NULL);
	int other_doc;
	if (find_unpaid_appointment_in_memory(patient_id, &other_doc) ||
	        has_active_appointment(doc_idx, patient_id) ||
	        !slot_available(doc_idx, visit, period, now))
		return NULL;
	if (g_last_app_id == LLONG_MAX) data_error("appointment ID limit");
	Appointment app = {++g_last_app_id, patient_id, g_doctors[doc_idx].info.id,
	                   visit, period, 0, now};
	update_appointment_in_file(app);
	insert_appointment_to_chain(doc_idx, app);
	return find_unpaid_appointment_in_memory(app.patient_id, &other_doc);
}

static int pay_appointment(AppointmentNode *node, int doc_idx)
{
	time_t now = time(NULL);
	if (pending_expired(&node->data, &g_doctors[doc_idx].info, now))
	{
		change_status(doc_idx, node, 4);
		return 0;
	}
	if (g_last_payment_id == LLONG_MAX) data_error("payment ID limit");
	PaymentTransaction tx;
	tx.app = node->data;
	tx.app.status = 1;
	tx.pay.id = ++g_last_payment_id;
	tx.pay.appointment_id = tx.app.id;
	tx.pay.amount = g_doctors[doc_idx].info.fee;
	tx.pay.paid = now;
	save_or_exit(TRANSACTION, &tx, 1);
	apply_payment(tx);
	node->data = tx.app;
	return 1;
}

static int reschedule_appointment(long patient_id, Appointment app, int doc_idx, time_t visit, int period)
{
	clean_all_expired_appointments();
	Appointment *old = find_appointment(app.id);
	if (!old || old->status != 4 || old->patient_id != patient_id ||
	        old->doctor_id != g_doctors[doc_idx].info.id ||
	        !is_appointment_paid(app.id) || has_active_appointment(doc_idx, app.patient_id) ||
	        !slot_available(doc_idx, visit, period, time(NULL)))
		return 0;
	app.status = 1;
	app.visit = visit;
	app.period = period;
	update_appointment_in_file(app);
	insert_appointment_to_chain(doc_idx, app);
	return 1;
}

static int save_capacity(int doc_idx, int am, int pm)
{
	clean_all_expired_appointments();
	Doctor doctors[MAX_DOCTORS];
	for (int i = 0; i < g_doctor_count; i++) doctors[i] = g_doctors[i].info;

	time_t now = time(NULL);
	Doctor *doc = &doctors[doc_idx];
	AppointmentNode *node = g_doctors[doc_idx].head;
	while (node)
	{
		time_t visit = node->data.visit;
		int period = node->data.period;
		int count = 0;
		do
		{
			count++;
			node = node->next;
		}
		while (node && node->data.visit == visit && node->data.period == period);
		int requested = period ? pm : am;
		int current = period ? doc->max_pm : doc->max_am;
		if (requested < current && requested < count && !is_expired_at(visit, period, doc, now))
			return 0;
	}
	doctors[doc_idx].max_am = am;
	doctors[doc_idx].max_pm = pm;
	save_or_exit(DOCTORS, doctors, g_doctor_count);
	g_doctors[doc_idx].info = doctors[doc_idx];
	return 1;
}

static int calculate_age(time_t birth)
{
	if (birth <= 0) return -1;
	int year = (int)(birth / 10000);
	int month = (int)(birth / 100 % 100);
	int day = (int)(birth % 100);
	time_t now = time(NULL);
	const struct tm *today = localtime(&now);
	if (!today) return -1;
	int age = today->tm_year + 1900 - year;
	if (today->tm_mon + 1 < month ||
	        (today->tm_mon + 1 == month && today->tm_mday < day))
		age--;
	return age;
}

static int parse_birth(const char *text, time_t *birth)
{
	if (strlen(text) != 10 || text[4] != '-' || text[7] != '-') return 0;
	char year_text[5], month_text[3], day_text[3];
	memcpy(year_text, text, 4); year_text[4] = '\0';
	memcpy(month_text, text + 5, 2); month_text[2] = '\0';
	memcpy(day_text, text + 8, 2); day_text[2] = '\0';
	int year, month, day;
	if (!parse_int(year_text, 1900, 9999, &year) ||
	        !parse_int(month_text, 1, 12, &month) ||
	        !parse_int(day_text, 1, 31, &day)) return 0;
	/* Use a positive day identifier so Windows localtime's pre-epoch limit
	   does not exclude birth dates before 1970. */
	int leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	const int days[] = {31, 28 + leap, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (day > days[month - 1]) return 0;
	time_t now = time(NULL);
	struct tm *today = localtime(&now);
	if (!today) return 0;
	int date = year * 10000 + month * 100 + day;
	int current = (today->tm_year + 1900) * 10000 + (today->tm_mon + 1) * 100 + today->tm_mday;
	if (date > current) return 0;
	*birth = (time_t)date;
	return 1;
}

#endif
