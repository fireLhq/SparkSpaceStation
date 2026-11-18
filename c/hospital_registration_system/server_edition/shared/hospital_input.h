#ifndef HOSPITAL_INPUT_H
#define HOSPITAL_INPUT_H

#include <ctype.h>
#include <errno.h>

static inline int read_line(FILE *fp, char *buffer, size_t size)
{
	size_t n = 0;
	int ch, valid = 1;
	while ((ch = fgetc(fp)) != EOF && ch != '\n')
	{
		if (ch == 0 || n == size - 1)
			valid = 0;
		else
			buffer[n++] = (char)ch;
	}
	if (n > 0 && buffer[n - 1] == '\r') n--;
	buffer[n] = '\0';
	if (ferror(fp) || (ch == EOF && !valid)) return -1;
	if (ch == EOF && n == 0) return 0;
	return valid ? 1 : -1;
}

static inline int parse_int(const char *text, int min, int max, int *out)
{
	if (!*text || strspn(text, "0123456789") != strlen(text)) return 0;
	errno = 0;
	long n = strtol(text, NULL, 10);
	if (errno || n < min || n > max) return 0;
	*out = (int)n;
	return 1;
}

/* -1 cancels on end of input, so callers can unwind their current menu. */
static inline int get_int_in_range(int min, int max, const char *prompt)
{
	char line[128];
	for (;;)
	{
		fputs(prompt, stdout);
		int result = read_line(stdin, line, sizeof(line));
		if (result == 0 || ferror(stdin)) return -1;
		int value;
		if (result == 1 && parse_int(line, min, max, &value)) return value;
		printf("Invalid input. Enter digits in the range %d-%d.\n", min, max);
	}
}

static inline int read_token(const char *prompt, char *out, size_t size)
{
	fputs(prompt, stdout);
	if (read_line(stdin, out, size) == 1 && *out && !strpbrk(out, " \t\r\v\f"))
		return 1;
	puts("Invalid input or length.");
	return 0;
}

#endif
