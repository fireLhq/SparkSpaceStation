#ifndef HOSPITAL_PROTOCOL_H
#define HOSPITAL_PROTOCOL_H

#include <stdint.h>

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT "8765"
#define WIRE_MAGIC 0x31535248u  /* HRS1 */
#define PAGE_ROWS 64
#define MAX_WIRE_ROWS 100
#define MAX_PAYLOAD (MAX_WIRE_ROWS * 228)
#define WIRE_PASSWORD_MAX 64

enum
{
	OP_LOGIN = 1, OP_REGISTER, OP_LOGOUT, OP_AI, OP_DEPARTMENTS, OP_DOCTORS,
	OP_DAYS, OP_PENDING, OP_BOOK, OP_PAYMENT, OP_APPOINTMENTS, OP_RESCHEDULE,
	OP_PAYMENTS, OP_QUEUE, OP_CALL, OP_FINISH, OP_CAPACITY
};

enum { ROLE_NONE, ROLE_PATIENT, ROLE_DOCTOR };
enum { RC_OK, RC_AUTH, RC_BAD, RC_UNAVAILABLE, RC_EMPTY, RC_BUSY };

 /* Version 1 uses little endian, binary32, packed records and CP936 text.
    Requests are fixed size. Responses have a header followed by zero or more rows. */
#pragma pack(push, 1)
typedef struct
{
	uint32_t magic;
	uint32_t op;
	int64_t id;
	int64_t visit;
	int32_t a, b;
	char name[16];
	char phone[16];
	char password[WIRE_PASSWORD_MAX + 1];
} WireRequest;

typedef struct
{
	uint32_t magic, status, rows, bytes;
	int32_t next;
} WireHeader;

typedef struct
{
	int32_t id;
	char name[32], location[64], description[128];
} WireDepartment;

typedef struct
{
	int32_t id, department_id;
	char name[16], title[16];
	int32_t gender;
	float fee;
	int32_t start_work_hour, end_work_hour, max_am, max_pm;
} WireDoctor;

typedef struct
{
	int64_t id;
	int32_t patient_id, doctor_id;
	int64_t visit, created;
	int32_t period, status, paid, seconds_left;
} WireAppointment;

typedef struct
{
	int64_t id, appointment_id, paid;
	float amount;
} WirePayment;

typedef struct
{
	int64_t visit;
	int32_t year, month, day;
	int32_t am_left, pm_left, am_open, pm_open;
} WireDay;

typedef struct
{
	char doc_name[16], dept_name[32], dept_location[64];
	int32_t end_hour, ahead_count, period;
} WireQueue;

typedef struct
{
	int32_t id;
	char name[16];
	int32_t role;
} WireAccount;

typedef struct
{
	int64_t id;
	int32_t patient_id;
	char name[16];
	int32_t gender, age;
} WireCall;

#pragma pack(pop)

#endif

