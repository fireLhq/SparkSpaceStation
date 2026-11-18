#ifndef DATA_STORE_H
#define DATA_STORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <io.h>
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#endif

#define FILE_DEPARTMENT "data/departments.bin"
#define FILE_DOCTOR "data/doctors.bin"
#define FILE_PATIENT "data/patients.bin"
#define FILE_APPOINTMENT "data/appointments.bin"
#define FILE_PAYMENT "data/payments.bin"
#define FILE_TRANSACTION "data/payment.pending"
#define MAX_DOCTORS 100
#define MAX_DEPARTMENTS 100
#define PASSWORD_MAX 64
#define PASSWORD_BYTES 97

typedef struct
{
	int id;
	char name[32];
	char location[64];
	char description[128];
} Department;

typedef struct
{
	int id;
	int department_id;
	char name[16];
	int gender;
	char title[16];
	float fee;
	char password[PASSWORD_BYTES];
	int start_work_hour;
	int end_work_hour;
	int max_am;
	int max_pm;
} Doctor;

typedef struct
{
	long id;
	char name[16];
	int gender;
	time_t birth;
	char phone[16];
	char password[PASSWORD_BYTES];
} Patient;

typedef struct
{
	long long id;
	long patient_id;
	int doctor_id;
	time_t visit;
	int period;
	int status;
	time_t created;
} Appointment;

typedef struct
{
	long long id;
	long long appointment_id;
	float amount;
	time_t paid;
} Payment;

typedef struct
{
	Appointment app;
	Payment pay;
} PaymentTransaction;

enum { DEPARTMENTS = 1, DOCTORS, PATIENTS, APPOINTMENTS, PAYMENTS, TRANSACTION };

static const char *const table_paths[] =
{
	NULL, FILE_DEPARTMENT, FILE_DOCTOR, FILE_PATIENT,
	FILE_APPOINTMENT, FILE_PAYMENT, FILE_TRANSACTION
};

static const size_t record_sizes[] =
{
	0, sizeof(Department), sizeof(Doctor), sizeof(Patient),
	sizeof(Appointment), sizeof(Payment), sizeof(PaymentTransaction)
};

/* Disk records have no native padding. Integers and binary32 are little-endian. */
static const size_t disk_sizes[] = {0, 228, 161, 145, 40, 28, 68};
static const size_t legacy_sizes[] = {0, 228, 72, 56, 40, 32, 0};

static inline int create_private_data_directory(void)
{
	DWORD attributes = GetFileAttributesA("data");
	if (attributes != INVALID_FILE_ATTRIBUTES)
		return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

	/* New data folders inherit access only for the current Windows user and SYSTEM. */
	HANDLE token;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 0;
	DWORD bytes = 0;
	GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
	TOKEN_USER *user = (TOKEN_USER *)malloc(bytes);
	LPSTR sid = NULL;
	int ok = user && GetTokenInformation(token, TokenUser, user, bytes, &bytes) &&
	         ConvertSidToStringSidA(user->User.Sid, &sid);
	CloseHandle(token);
	free(user);
	if (!ok) return 0;

	char acl[256];
	int size = snprintf(acl, sizeof(acl), "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;%s)", sid);
	LocalFree(sid);
	PSECURITY_DESCRIPTOR descriptor;
	if (size < 0 || (size_t)size >= sizeof(acl) ||
	        !ConvertStringSecurityDescriptorToSecurityDescriptorA(acl, SDDL_REVISION_1,
	                                                               &descriptor, NULL))
		return 0;
	SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
	ok = CreateDirectoryA("data", &security) || GetLastError() == ERROR_ALREADY_EXISTS;
	LocalFree(descriptor);
	return ok;
}

static inline int lock_database(void)
{
	if (!create_private_data_directory()) return 0;
	/* Held by the process until it exits, including on abnormal termination. */
	return CreateFileA("data/.lock", GENERIC_READ | GENERIC_WRITE, 0, NULL,
	                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) != INVALID_HANDLE_VALUE;
}

static inline void encode_hex(char *out, const unsigned char *bytes, size_t count)
{
	const char *hex = "0123456789abcdef";
	for (size_t i = 0; i < count; i++)
	{
		out[i * 2] = hex[bytes[i] >> 4];
		out[i * 2 + 1] = hex[bytes[i] & 15];
	}
	out[count * 2] = '\0';
}

static inline int valid_password_input(const char *text)
{
	if (!*text) return 0;
	for (; *text; text++)
		if ((unsigned char)*text < 33 || (unsigned char)*text > 126) return 0;
	return 1;
}

static inline int password_digest(const char *password, unsigned char *salt, unsigned char *digest)
{
	BCRYPT_ALG_HANDLE alg;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL,
	                               BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0)
		return 0;
	NTSTATUS result = BCryptDeriveKeyPBKDF2(alg, (PUCHAR)password, (ULONG)strlen(password),
	                                       salt, 16, 600000, digest, 32, 0);
	BCryptCloseAlgorithmProvider(alg, 0);
	return result >= 0;
}

static inline int hash_password(const char *password, char *out)
{
	unsigned char salt[16], digest[32];
	if (BCryptGenRandom(NULL, salt, sizeof(salt), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 ||
	        !password_digest(password, salt, digest))
		return 0;
	encode_hex(out, salt, sizeof(salt));
	encode_hex(out + 32, digest, sizeof(digest));
	SecureZeroMemory(digest, sizeof(digest));
	return 1;
}

static inline int valid_hash(const char *hash)
{
	return strlen(hash) == PASSWORD_BYTES - 1 &&
	       strspn(hash, "0123456789abcdef") == PASSWORD_BYTES - 1;
}

static inline int verify_password(const char *password, const char *hash)
{
	if (!valid_hash(hash)) return 0;
	unsigned char salt[16], digest[32];
	char computed[65];
	for (size_t i = 0; i < sizeof(salt); i++)
	{
		char pair[] = {hash[i * 2], hash[i * 2 + 1], '\0'};
		salt[i] = (unsigned char)strtoul(pair, NULL, 16);
	}
	if (!password_digest(password, salt, digest)) return 0;
	encode_hex(computed, digest, sizeof(digest));
	unsigned char diff = 0;
	for (int i = 0; i < 64; i++) diff |= (unsigned char)(computed[i] ^ hash[32 + i]);
	SecureZeroMemory(digest, sizeof(digest));
	SecureZeroMemory(computed, sizeof(computed));
	return diff == 0;
}

static inline uint64_t read_number(const unsigned char *p, size_t width)
{
	uint64_t value = 0;
	for (size_t i = 0; i < width; i++) value |= (uint64_t)p[i] << (i * 8);
	return value;
}

static inline void write_number(unsigned char *p, size_t width, uint64_t value)
{
	for (size_t i = 0; i < width; i++) p[i] = (unsigned char)(value >> (i * 8));
}

static inline int read_text(char *dest, const unsigned char *src, size_t size)
{
	const unsigned char *end = (const unsigned char *)memchr(src, 0, size);
	if (!end) return 0;
	memset(dest, 0, size);
	memcpy(dest, src, (size_t)(end - src));
	return 1;
}

static inline void write_text(unsigned char *dest, const char *src, size_t size)
{
	memset(dest, 0, size);
	memcpy(dest, src, strlen(src));
}

static inline int record_codec(int table, void *record, unsigned char *p, int writing, int legacy)
{
#define NUMBER(field, width, type) \
	do \
	{ \
		if (writing) write_number(p, width, (uint64_t)(field)); \
		else (field) = (type)read_number(p, width); \
		p += width; \
	} while (0)
#define RECORD_TEXT(field) \
	do \
	{ \
		if (writing) write_text(p, field, sizeof(field)); \
		else if (!read_text(field, p, sizeof(field))) return 0; \
		p += sizeof(field); \
	} while (0)
#define PRICE(field) \
	do \
	{ \
		uint32_t bits = 0; \
		if (writing) memcpy(&bits, &(field), 4); \
		NUMBER(bits, 4, uint32_t); \
		if (!writing) memcpy(&(field), &bits, 4); \
	} while (0)
#define PASSWORD(field) \
	do \
	{ \
		if (legacy) \
		{ \
			char old[8] = {0}; \
			int ok = read_text(old, p, sizeof(old)) && hash_password(old, field); \
			SecureZeroMemory(old, sizeof(old)); \
			if (!ok) return 0; \
			p += 8; \
		} \
		else \
		{ \
			RECORD_TEXT(field); \
			if (!valid_hash(field)) return 0; \
		} \
	} while (0)
	switch (table)
	{
		case DEPARTMENTS:
		{
			Department *d = (Department *)record;
			NUMBER(d->id, 4, int32_t);
			RECORD_TEXT(d->name);
			RECORD_TEXT(d->location);
			RECORD_TEXT(d->description);
			return d->id > 0 && d->name[0];
		}
		case DOCTORS:
		{
			Doctor *d = (Doctor *)record;
			NUMBER(d->id, 4, int32_t);
			NUMBER(d->department_id, 4, int32_t);
			RECORD_TEXT(d->name);
			NUMBER(d->gender, 4, int32_t);
			RECORD_TEXT(d->title);
			PRICE(d->fee);
			PASSWORD(d->password);
			NUMBER(d->start_work_hour, 4, int32_t);
			NUMBER(d->end_work_hour, 4, int32_t);
			NUMBER(d->max_am, 4, int32_t);
			NUMBER(d->max_pm, 4, int32_t);
			return d->id > 0 && d->department_id > 0 && d->name[0] && d->title[0] &&
			       d->gender >= 0 && d->gender <= 1 && isfinite(d->fee) && d->fee >= 0 &&
			       d->start_work_hour >= 0 && d->start_work_hour < d->end_work_hour &&
			       d->end_work_hour <= 24 &&
			       d->max_am >= 0 && d->max_am <= 99 && d->max_pm >= 0 && d->max_pm <= 99;
		}
		case PATIENTS:
		{
			Patient *a = (Patient *)record;
			NUMBER(a->id, 4, int32_t);
			RECORD_TEXT(a->name);
			NUMBER(a->gender, 4, int32_t);
			NUMBER(a->birth, 8, int64_t);
			if (legacy) a->birth = 0; /* Old registration stored its own timestamp. */
			RECORD_TEXT(a->phone);
			PASSWORD(a->password);
			return a->id >= 100000001 && a->id <= 999999999 && a->name[0] && a->phone[0];
		}
		case APPOINTMENTS:
		{
			Appointment *a = (Appointment *)record;
			NUMBER(a->id, 8, int64_t);
			NUMBER(a->patient_id, 4, int32_t);
			NUMBER(a->doctor_id, 4, int32_t);
			NUMBER(a->visit, 8, int64_t);
			NUMBER(a->period, 4, int32_t);
			NUMBER(a->status, 4, int32_t);
			NUMBER(a->created, 8, int64_t);
			return a->id > 0 && a->visit > 0 && localtime(&a->visit) != NULL &&
			       (a->period == 0 || a->period == 1) &&
			       ((a->status >= 0 && a->status <= 4) || legacy) &&
			       a->created > 0 && localtime(&a->created) != NULL;
		}
		case PAYMENTS:
		{
			Payment *a = (Payment *)record;
			NUMBER(a->id, 8, int64_t);
			NUMBER(a->appointment_id, 8, int64_t);
			PRICE(a->amount);
			if (legacy) p += 4;
			NUMBER(a->paid, 8, int64_t);
			return a->id > 0 && a->appointment_id > 0 && a->paid > 0 &&
			       localtime(&a->paid) != NULL && isfinite(a->amount) && a->amount >= 0;
		}
		case TRANSACTION:
		{
			PaymentTransaction *tx = (PaymentTransaction *)record;
			return record_codec(APPOINTMENTS, &tx->app, p, writing, 0) &&
			       record_codec(PAYMENTS, &tx->pay, p + disk_sizes[APPOINTMENTS], writing, 0) &&
			       tx->app.status == 1 && tx->app.id == tx->pay.appointment_id;
		}
	}
	return 0;
#undef NUMBER
#undef RECORD_TEXT
#undef PRICE
#undef PASSWORD
}

/* Read the complete table or fail. A missing file is an empty table. */
static inline int load_table(int table, void **items, int *count, int *legacy)
{
	*items = NULL;
	*count = 0;
	*legacy = 0;
	FILE *fp = fopen(table_paths[table], "rb");
	if (!fp) return errno == ENOENT;

	unsigned char header[8], data[256];
	size_t n = fread(header, 1, sizeof(header), fp);
	int ok = !ferror(fp);
	size_t width = disk_sizes[table];
	long offset = 0;
	if (n >= 4 && memcmp(header, "HRDB", 4) == 0)
	{
		ok = ok && n == 8 && header[4] == 1 && header[5] == table &&
		     header[6] == 0 && header[7] == 0;
		offset = 8;
	}
	else
	{
		*legacy = 1;
		width = legacy_sizes[table];
	}
	long length = -1;
	if (ok && fseek(fp, 0, SEEK_END) == 0) length = ftell(fp);
	if (width == 0 || length < offset || (length - offset) % (long)width != 0 ||
	        fseek(fp, offset, SEEK_SET) != 0)
		ok = 0;
	if (ok)
	{
		*count = (int)((length - offset) / (long)width);
		size_t stride = record_sizes[table];
		if ((size_t)*count > SIZE_MAX / stride) ok = 0;
		if (ok && *count > 0)
		{
			*items = calloc((size_t)*count, stride);
			ok = *items != NULL;
		}
		for (int i = 0; ok && i < *count; i++)
		{
			void *record = (unsigned char *)*items + (size_t)i * stride;
			ok = fread(data, 1, width, fp) == width &&
			     record_codec(table, record, data, 0, *legacy);
		}
	}
	if (fclose(fp) != 0) ok = 0;
	if (!ok)
	{
		free(*items);
		*items = NULL;
		*count = 0;
	}
	return ok;
}

static inline int save_table(int table, void *items, int count)
{
	char temp[80];
	snprintf(temp, sizeof(temp), "%s.tmp", table_paths[table]);
	FILE *fp = fopen(temp, "wb");
	if (!fp) return 0;

	unsigned char header[] = {'H', 'R', 'D', 'B', 1, (unsigned char)table, 0, 0};
	unsigned char data[256];
	size_t width = disk_sizes[table], stride = record_sizes[table];
	int ok = fwrite(header, 1, sizeof(header), fp) == sizeof(header);
	for (int i = 0; ok && i < count; i++)
	{
		void *record = (unsigned char *)items + (size_t)i * stride;
		ok = record_codec(table, record, data, 1, 0) && fwrite(data, 1, width, fp) == width;
	}
	if (ok) ok = fflush(fp) == 0 && _commit(_fileno(fp)) == 0;
	if (fclose(fp) != 0) ok = 0;
	if (ok)
		ok = MoveFileExA(temp, table_paths[table],
		                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	if (!ok) DeleteFileA(temp);
	return ok;
}

#endif
