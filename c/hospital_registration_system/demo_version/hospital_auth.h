#ifndef HOSPITAL_AUTH_H
#define HOSPITAL_AUTH_H

static int read_patient_id(void)
{
	char line[10];
	int id;
	if (read_token("九位账号: ", line, sizeof(line)) &&
	        strlen(line) == 9 && parse_int(line, 100000001, 999999999, &id))
		return id;
	puts("账号应为 100000001 到 999999999 之间的九位数字。");
	return -1;
}

static int patient_login(void)
{
	printf("\n--- 患者登录 ---\n");
	int id = read_patient_id();
	char password[PASSWORD_MAX + 1];
	if (id < 0 || !read_token("密码: ", password, sizeof(password))) return 0;
	Patient p;
	int ok = find_patient_by_id(id, &p) && verify_password(password, p.password);
	SecureZeroMemory(password, sizeof(password));
	if (!ok)
	{
		printf("账号或密码错误！\n");
		return 0;
	}
	g_current_patient = p;
	printf("登录成功！欢迎，%s\n", p.name);
	return 1;
}

static void patient_register(void)
{
	Patient p;
	memset(&p, 0, sizeof(p));
	printf("\n--- 患者注册 ---\n");
	p.id = read_patient_id();
	if (p.id < 0) return;
	if (find_patient_by_id(p.id, NULL))
	{
		printf("该账号已被注册！\n");
		return;
	}
	if (!read_token("姓名（最多15字节）: ", p.name, sizeof(p.name))) return;
	p.gender = get_int_in_range(0, 1, "性别 (0-男, 1-女): ");
	if (p.gender < 0) return;
	char birth[11];
	if (!read_token("出生日期 (YYYY-MM-DD): ", birth, sizeof(birth)) ||
	        !parse_birth(birth, &p.birth))
	{
		printf("出生日期无效！\n");
		return;
	}
	if (!read_token("电话号码（最多15位数字）: ", p.phone, sizeof(p.phone))) return;
	if (strspn(p.phone, "0123456789") != strlen(p.phone))
	{
		printf("电话号码只能包含数字！\n");
		return;
	}
	char password[PASSWORD_MAX + 1];
	if (!read_token("密码（1-64个ASCII非空白字符）: ", password, sizeof(password))) return;
	int valid = valid_password_input(password);
	int saved = valid && hash_password(password, p.password);
	SecureZeroMemory(password, sizeof(password));
	if (!saved)
	{
		printf("密码格式无效或无法保存。\n");
		return;
	}
	save_patient_sorted(p);
	printf("注册成功！请记住您的账号：%ld\n", p.id);
}

static int doctor_login(void)
{
	printf("\n--- 医生端登录 ---\n");
	int id = get_int_in_range(1, INT_MAX, "工号: ");
	char password[PASSWORD_MAX + 1];
	if (id < 0 || !read_token("密码: ", password, sizeof(password))) return 0;
	int idx = find_doctor_index(id);
	int ok = idx >= 0 && verify_password(password, g_doctors[idx].info.password);
	SecureZeroMemory(password, sizeof(password));
	if (!ok)
	{
		printf("工号或密码错误！\n");
		return 0;
	}
	g_current_doctor = g_doctors[idx].info;
	printf("登录成功！欢迎 %s 医师！\n", g_current_doctor.name);
	return 1;
}

#endif
