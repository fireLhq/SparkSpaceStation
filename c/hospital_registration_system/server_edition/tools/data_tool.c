#include "../server/data_store.h"
#include "../shared/hospital_input.h"
#include <direct.h>

typedef struct
{
	unsigned char *rows;
	int count, capacity;
} Table;

typedef union
{
	Department dept;
	Doctor doctor;
	Patient patient;
	Appointment app;
	Payment pay;
} Record;

static Table tables[PAYMENTS + 1];
static char tool_dir[MAX_PATH], server_dir[MAX_PATH];

static const char *const table_names[] =
{
	NULL, "departments", "doctors", "patients", "appointments", "payments"
};

static const char *const formats[] =
{
	NULL,
	"id name location description",
	"id department_id name gender title fee password start_hour end_hour max_am max_pm",
	"id name gender YYYYMMDD phone password",
	"id patient_id doctor_id visit_timestamp period status created_timestamp",
	"id appointment_id amount paid_timestamp"
};

static void *row_at(int table, const Table *list, int index)
{
	return list->rows + (size_t)index * record_sizes[table];
}

static long long get_id(int table, const void *row)
{
	switch (table)
	{
		case DEPARTMENTS: return ((const Department *)row)->id;
		case DOCTORS: return ((const Doctor *)row)->id;
		case PATIENTS: return ((const Patient *)row)->id;
		case APPOINTMENTS: return ((const Appointment *)row)->id;
		default: return ((const Payment *)row)->id;
	}
}

static int find_index(int table, const Table *list, long long id)
{
	for (int i = 0; i < list->count; i++)
		if (get_id(table, row_at(table, list, i)) == id) return i;
	return -1;
}

static int append(int table, Table *list, const void *record)
{
	if (list->count == list->capacity)
	{
		size_t stride = record_sizes[table];
		int cap = list->capacity ? (list->capacity <= INT_MAX / 2 ? list->capacity * 2 : INT_MAX) : 16;
		if (list->count == INT_MAX || (size_t)cap > SIZE_MAX / stride) return 0;
		unsigned char *buffer = (unsigned char *)realloc(list->rows, (size_t)cap * stride);
		if (!buffer) return 0;
		list->rows = buffer;
		list->capacity = cap;
	}
	memcpy(row_at(table, list, list->count++), record, record_sizes[table]);
	return 1;
}

static int split_fields(char *line, char **fields, int max)
{
	int count = 0;
	for (char *part = strtok(line, " \t\r\n"); part; part = strtok(NULL, " \t\r\n"))
	{
		if (count == max) return -1;
		fields[count++] = part;
	}
	return count;
}

static int positive_id(const char *text, long long *out)
{
	if (!*text || strspn(text, "0123456789") != strlen(text)) return 0;
	errno = 0;
	long long value = strtoll(text, NULL, 10);
	if (errno || value < 1) return 0;
	*out = value;
	return 1;
}

static int copy_text(char *dest, size_t size, const char *input)
{
	size_t length = strlen(input);
	if (!length || length >= size) return 0;
	for (size_t i = 0; i < length; i++)
	{
		unsigned char ch = (unsigned char)input[i];
		if (ch <= 32 || ch == 127) return 0;
	}
	memcpy(dest, input, length + 1);
	return 1;
}

static int read_price(const char *text, float *out)
{
	char *end;
	errno = 0;
	float price = strtof(text, &end);
	if (end == text || *end || errno || !isfinite(price) || price < 0) return 0;
	*out = price;
	return 1;
}

static int read_timestamp(const char *text, time_t *out)
{
	long long value;
	if (!positive_id(text, &value)) return 0;
	*out = (time_t)value;
	return (long long)*out == value && localtime(out) != NULL;
}

static int valid_birth(int birth)
{
	if (!birth) return 1; /* Unknown, e.g. migrated legacy records. */
	int year = birth / 10000, month = birth / 100 % 100, day = birth % 100;
	int leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	const int days[] = {31, 28 + leap, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	time_t now = time(NULL);
	const struct tm *today = localtime(&now);
	return today && year >= 1900 && month >= 1 && month <= 12 && day >= 1 &&
	       day <= days[month - 1] && birth <= (today->tm_year + 1900) * 10000 +
	       (today->tm_mon + 1) * 100 + today->tm_mday;
}

static int set_password(char *out, const char *input, const char *old)
{
	if (old && strcmp(input, "-") == 0)
	{
		strcpy(out, old);
		return 1;
	}
	return strlen(input) <= PASSWORD_MAX && valid_password_input(input) && hash_password(input, out);
}

static int parse_record(int table, char **f, int count, Record *record, const void *old)
{
	memset(record, 0, sizeof(*record));
	switch (table)
	{
		case DEPARTMENTS:
		{
			Department *d = &record->dept;
			return count == 4 && parse_int(f[0], 1, INT_MAX, &d->id) &&
			       copy_text(d->name, sizeof(d->name), f[1]) &&
			       copy_text(d->location, sizeof(d->location), f[2]) &&
			       copy_text(d->description, sizeof(d->description), f[3]);
		}
		case DOCTORS:
		{
			Doctor *d = &record->doctor;
			return count == 11 && parse_int(f[0], 1, INT_MAX, &d->id) &&
			       parse_int(f[1], 1, INT_MAX, &d->department_id) &&
			       copy_text(d->name, sizeof(d->name), f[2]) &&
			       parse_int(f[3], 0, 1, &d->gender) &&
			       copy_text(d->title, sizeof(d->title), f[4]) && read_price(f[5], &d->fee) &&
			       parse_int(f[7], 0, 23, &d->start_work_hour) &&
			       parse_int(f[8], d->start_work_hour + 1, 24, &d->end_work_hour) &&
			       parse_int(f[9], 0, 99, &d->max_am) && parse_int(f[10], 0, 99, &d->max_pm) &&
			       set_password(d->password, f[6], old ? ((const Doctor *)old)->password : NULL);
		}
		case PATIENTS:
		{
			Patient *p = &record->patient;
			int id, birth;
			if (count != 6 || !parse_int(f[0], 100000001, 999999999, &id) ||
			        !copy_text(p->name, sizeof(p->name), f[1]) || !parse_int(f[2], 0, 1, &p->gender) ||
			        !parse_int(f[3], 0, 99991231, &birth) || !valid_birth(birth) ||
			        !copy_text(p->phone, sizeof(p->phone), f[4]) ||
			        strspn(f[4], "0123456789") != strlen(f[4])) return 0;
			p->id = id;
			p->birth = (time_t)birth;
			return set_password(p->password, f[5], old ? ((const Patient *)old)->password : NULL);
		}
		case APPOINTMENTS:
		{
			Appointment *a = &record->app;
			int id;
			if (count != 7 || !positive_id(f[0], &a->id) ||
			        !parse_int(f[1], 100000001, 999999999, &id) ||
			        !parse_int(f[2], 1, INT_MAX, &a->doctor_id) ||
			        !read_timestamp(f[3], &a->visit) || !parse_int(f[4], 0, 1, &a->period) ||
			        !parse_int(f[5], 0, 4, &a->status) || !read_timestamp(f[6], &a->created))
				return 0;
			a->patient_id = id;
			const struct tm *visit = localtime(&a->visit);
			return visit && !visit->tm_hour && !visit->tm_min && !visit->tm_sec;
		}
		case PAYMENTS:
		{
			Payment *p = &record->pay;
			return count == 4 && positive_id(f[0], &p->id) &&
			       positive_id(f[1], &p->appointment_id) &&
			       read_price(f[2], &p->amount) && read_timestamp(f[3], &p->paid);
		}
	}
	return 0;
}

static int compare_rows(void *context, const void *a, const void *b)
{
	int table = *(int *)context;
	long long first = get_id(table, a), second = get_id(table, b);
	return (first > second) - (first < second);
}

static int validate_change(const Table *db)
{
	if (db[DEPARTMENTS].count > MAX_DEPARTMENTS || db[DOCTORS].count > MAX_DOCTORS)
	{
		puts("Maximum is 100 departments and 100 doctors.");
		return 0;
	}
	for (int i = 0; i < db[DOCTORS].count; i++)
	{
		const Doctor *doctor = (const Doctor *)row_at(DOCTORS, &db[DOCTORS], i);
		if (find_index(DEPARTMENTS, &db[DEPARTMENTS], doctor->department_id) < 0)
		{
			printf("Doctor %d references missing department %d.\n", doctor->id, doctor->department_id);
			return 0;
		}
	}

	unsigned char *paid = NULL;
	if (db[APPOINTMENTS].count)
	{
		paid = (unsigned char *)calloc((size_t)db[APPOINTMENTS].count, 1);
		if (!paid)
		{
			puts("Out of memory.");
			return 0;
		}
	}
	int valid = 1;
	for (int i = 0; i < db[PAYMENTS].count && valid; i++)
	{
		const Payment *pay = (const Payment *)row_at(PAYMENTS, &db[PAYMENTS], i);
		int pos = find_index(APPOINTMENTS, &db[APPOINTMENTS], pay->appointment_id);
		if (pos < 0 || paid[pos])
		{
			printf("Payment %lld: missing appointment or more than one payment for appointment %lld.\n",
			       pay->id, pay->appointment_id);
			valid = 0;
		}
		else paid[pos] = 1;
	}
	for (int i = 0; i < db[APPOINTMENTS].count && valid; i++)
	{
		const Appointment *app = (const Appointment *)row_at(APPOINTMENTS, &db[APPOINTMENTS], i);
		if (find_index(PATIENTS, &db[PATIENTS], app->patient_id) < 0 ||
		        find_index(DOCTORS, &db[DOCTORS], app->doctor_id) < 0)
		{
			printf("Appointment %lld references a missing patient or doctor.\n", app->id);
			valid = 0;
		}
		else if ((app->status == 0 && paid[i]) ||
		         (app->status >= 1 && app->status <= 3 && !paid[i]))
		{
			printf("Appointment %lld: status %d conflicts with its payment record.\n",
			       app->id, app->status);
			valid = 0;
		}
	}
	free(paid);
	return valid;
}

static void commit(int table, Table *candidate)
{
	if (candidate->count > 1)
		qsort_s(candidate->rows, (size_t)candidate->count, record_sizes[table], compare_rows, &table);
	for (int i = 1; i < candidate->count; i++)
	{
		if (get_id(table, row_at(table, candidate, i - 1)) == get_id(table, row_at(table, candidate, i)))
		{
			puts("Duplicate primary key. No changes saved.");
			return;
		}
	}
	Table db[PAYMENTS + 1];
	memcpy(db, tables, sizeof(db));
	db[table] = *candidate;
	if (!validate_change(db))
	{
		puts("No changes saved.");
		return;
	}
	if (!save_table(table, candidate->rows, candidate->count))
	{
		puts("Write failed. No changes saved.");
		return;
	}
	free(tables[table].rows);
	tables[table] = *candidate;
	memset(candidate, 0, sizeof(*candidate));
	printf("Saved %s. Total: %d.\n", table_names[table], tables[table].count);
}

static int copy_table(int table, Table *candidate)
{
	Table *source = &tables[table];
	if (!source->count) return 1;
	size_t bytes = (size_t)source->count * record_sizes[table];
	candidate->rows = (unsigned char *)malloc(bytes);
	if (!candidate->rows)
	{
		puts("Out of memory. No changes saved.");
		return 0;
	}
	memcpy(candidate->rows, source->rows, bytes);
	candidate->count = candidate->capacity = source->count;
	return 1;
}

static FILE *open_import(int table)
{
	char name[MAX_PATH], default_name[32];
	snprintf(default_name, sizeof(default_name), "%s.txt", table_names[table]);
	printf("TXT file [%s]: ", default_name);
	if (read_line(stdin, name, sizeof(name)) != 1) return NULL;
	if (!*name) strcpy(name, default_name);
	if (_chdir(tool_dir) != 0) return NULL;
	FILE *fp = fopen(name, "r");
	if (_chdir(server_dir) != 0)
	{
		fputs("Lost server directory.\n", stderr);
		exit(EXIT_FAILURE);
	}
	if (!fp) puts("TXT could not be opened.");
	return fp;
}

static int confirm_replace(void)
{
	char line[16];
	fputs("This removes rows from the selected table. Type YES to continue: ", stdout);
	return read_line(stdin, line, sizeof(line)) == 1 && strcmp(line, "YES") == 0;
}

static void import_rows(int table, int sync)
{
	printf("TXT format: %s\n", formats[table]);
	FILE *fp = open_import(table);
	if (!fp) return;
	Table candidate = {0};
	if (!sync && !copy_table(table, &candidate))
	{
		fclose(fp);
		return;
	}
	char line[1024], *fields[11];
	int result, line_no = 0, duplicates = 0, added = 0;
	while ((result = read_line(fp, line, sizeof(line))) != 0)
	{
		line_no++;
		int count = result == 1 ? split_fields(line, fields, 11) : -1;
		if (count == 0) continue;
		long long id;
		if (count < 0 || !positive_id(fields[0], &id)) break;
		if (find_index(table, &candidate, id) >= 0)
		{
			duplicates++;
			continue;
		}
		Record record;
		int ok = parse_record(table, fields, count, &record, NULL);
		SecureZeroMemory(line, sizeof(line));
		if (!ok || !append(table, &candidate, &record)) break;
		added++;
	}
	SecureZeroMemory(line, sizeof(line));
	if (fclose(fp) != 0) result = -1;
	if (result != 0)
		printf("Invalid data or read error at line %d. No changes saved.\n", line_no);
	else if (!sync || confirm_replace())
	{
		printf("Imported candidates: %d. Duplicate keys skipped: %d.\n", added, duplicates);
		if (sync || added) commit(table, &candidate);
	}
	free(candidate.rows);
}

static void update_or_delete(int table, int editing)
{
	char id_text[32];
	long long id;
	if (!read_token("Primary key: ", id_text, sizeof(id_text)) || !positive_id(id_text, &id))
	{
		puts("Invalid key.");
		return;
	}
	int pos = find_index(table, &tables[table], id);
	if (pos < 0)
	{
		puts("Record not found.");
		return;
	}
	Table candidate = {0};
	if (!copy_table(table, &candidate)) return;
	void *row = row_at(table, &candidate, pos);
	if (editing)
	{
		printf("Enter the full row (%s). Keep its key; password '-' keeps the current password.\n",
		       formats[table]);
		char line[1024], *fields[11];
		int count = read_line(stdin, line, sizeof(line)) == 1 ? split_fields(line, fields, 11) : -1;
		Record record;
		int valid = parse_record(table, fields, count, &record, row) && get_id(table, &record) == id;
		SecureZeroMemory(line, sizeof(line));
		if (valid)
		{
			memcpy(row, &record, record_sizes[table]);
			commit(table, &candidate);
		}
		else puts("Invalid row or key changed. No changes saved.");
	}
	else
	{
		if (pos + 1 < candidate.count)
			memmove(row, row_at(table, &candidate, pos + 1),
			        (size_t)(candidate.count - pos - 1) * record_sizes[table]);
		candidate.count--;
		commit(table, &candidate);
	}
	free(candidate.rows);
}

static void show_table(int table)
{
	const Table *list = &tables[table];
	printf("\n%s | %s\n", table_names[table], formats[table]);
	for (int i = 0; i < list->count; i++)
	{
		const void *row = row_at(table, list, i);
		switch (table)
		{
			case DEPARTMENTS:
			{
				const Department *d = (const Department *)row;
				printf("%d %s %s %s\n", d->id, d->name, d->location, d->description);
				break;
			}
			case DOCTORS:
			{
				const Doctor *d = (const Doctor *)row;
				printf("%d %d %s %d %s %.9g [hidden] %d %d %d %d\n", d->id, d->department_id,
				       d->name, d->gender, d->title, d->fee, d->start_work_hour,
				       d->end_work_hour, d->max_am, d->max_pm);
				break;
			}
			case PATIENTS:
			{
				const Patient *p = (const Patient *)row;
				printf("%ld %s %d %08lld %s [hidden]\n", p->id, p->name, p->gender,
				       (long long)p->birth, p->phone);
				break;
			}
			case APPOINTMENTS:
			{
				const Appointment *a = (const Appointment *)row;
				printf("%lld %ld %d %lld %d %d %lld\n", a->id, a->patient_id, a->doctor_id,
				       (long long)a->visit, a->period, a->status, (long long)a->created);
				break;
			}
			case PAYMENTS:
			{
				const Payment *p = (const Payment *)row;
				printf("%lld %lld %.9g %lld\n", p->id, p->appointment_id, p->amount, (long long)p->paid);
				break;
			}
		}
	}
	printf("Total: %d.\n", list->count);
}

static int enter_server_directory(void)
{
	DWORD length = GetModuleFileNameA(NULL, tool_dir, sizeof(tool_dir));
	if (!length || length >= sizeof(tool_dir)) return 0;
	char *slash = strrchr(tool_dir, '\\');
	if (!slash) return 0;
	*slash = '\0';
	return _chdir(tool_dir) == 0 && _chdir("../server") == 0 &&
	       _getcwd(server_dir, sizeof(server_dir)) != NULL;
}

int main(void)
{
	if (!enter_server_directory())
	{
		fputs("Keep the tool in server_edition/tools beside the server directory.\n", stderr);
		return 1;
	}
	if (!lock_database())
	{
		fputs("Cannot lock server/data. Stop the server or the other data tool first.\n", stderr);
		return 1;
	}
	void *records;
	int count, legacy;
	if (!load_table(TRANSACTION, &records, &count, &legacy) || count)
	{
		free(records);
		fputs("Start and stop the server to recover the pending payment first.\n", stderr);
		return 1;
	}
	free(records);
	for (int table = DEPARTMENTS; table <= PAYMENTS; table++)
	{
		if (!load_table(table, &records, &count, &legacy))
		{
			fprintf(stderr, "Cannot read %s.\n", table_paths[table]);
			return 1;
		}
		tables[table].rows = (unsigned char *)records;
		tables[table].count = tables[table].capacity = count;
	}
	for (;;)
	{
		puts("\n[1] Departments [2] Doctors [3] Patients [4] Appointments [5] Payments [0] Exit");
		int table = get_int_in_range(0, 5, "Table: ");
		if (table <= 0) break;
		printf("\n%s: [1] Show all [2] Import TXT [3] Edit one [4] Delete one "
		       "[5] Clear [6] Sync TXT [0] Back\n", table_names[table]);
		int op = get_int_in_range(0, 6, "Operation: ");
		switch (op)
		{
			case 1: show_table(table); break;
			case 2: import_rows(table, 0); break;
			case 3: update_or_delete(table, 1); break;
			case 4: update_or_delete(table, 0); break;
			case 5:
				if (confirm_replace())
				{
					Table empty = {0};
					commit(table, &empty);
				}
				break;
			case 6: import_rows(table, 1); break;
		}
		if (op < 0) break;
	}
	for (int table = DEPARTMENTS; table <= PAYMENTS; table++) free(tables[table].rows);
	return 0;
}
