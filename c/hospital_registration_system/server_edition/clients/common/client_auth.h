#ifndef HOSPITAL_CLIENT_AUTH_H
#define HOSPITAL_CLIENT_AUTH_H

static inline int read_patient_id(void)
{
	char line[10];
	int id;
	if (read_token("九位账号: ", line, sizeof(line)) && strlen(line) == 9 &&
	        parse_int(line, 100000001, 999999999, &id)) return id;
	puts("请输入符合范围的九位数字账号。");
	return -1;
}

static inline int client_login(int role)
{
	WireRequest req = request_for(OP_LOGIN);
	req.id = role == ROLE_PATIENT ? read_patient_id() : get_int_in_range(1, INT_MAX, "医生工号: ");
	req.a = role;
	if (req.id < 0 || !read_token("密码: ", req.password, sizeof(req.password))) return 0;
	WireHeader response = exchange(&req, &current_account, sizeof(current_account), 1);
	SecureZeroMemory(req.password, sizeof(req.password));
	if (response.rows != 1)
	{
		puts(response.status == RC_BUSY ? "该账号已在另一客户端登录。" : "账号或密码错误。");
		return 0;
	}
	printf("登录成功，欢迎 %s。\n", current_account.name);
	fetch_catalog();
	return 1;
}

static inline void patient_register(void)
{
	WireRequest req = request_for(OP_REGISTER);
	req.id = read_patient_id();
	if (req.id < 0 || !read_token("姓名（最多15字节）: ", req.name, sizeof(req.name))) return;
	req.a = get_int_in_range(0, 1, "性别 (0-男, 1-女): ");
	if (req.a < 0) return;
	char birth[11];
	if (!read_token("出生日期 (YYYY-MM-DD): ", birth, sizeof(birth)) ||
	        strlen(birth) != 10 || birth[4] != '-' || birth[7] != '-')
	{
		puts("出生日期格式有误。");
		return;
	}
	birth[4] = birth[7] = '\0';
	int year, month, day;
	if (!parse_int(birth, 1900, 9999, &year) ||
	        !parse_int(birth + 5, 1, 12, &month) ||
	        !parse_int(birth + 8, 1, 31, &day))
	{
		puts("出生日期格式有误。");
		return;
	}
	req.b = year * 10000 + month * 100 + day;
	if (!read_token("电话（最多15位数字）: ", req.phone, sizeof(req.phone)) ||
	        !read_token("密码（1-64个ASCII非空白字符）: ", req.password, sizeof(req.password))) return;
	WireHeader response = exchange(&req, NULL, 0, 0);
	SecureZeroMemory(req.password, sizeof(req.password));
	if (response.status == RC_OK) printf("注册成功，账号 %lld。\n", (long long)req.id);
	else puts(response.status == RC_BUSY ? "账号已被注册。" : "注册失败，请检查输入。");
}

static inline void start_local_ai(void)
{
	if (!rpc_action(OP_AI, 0, 0, 0, 0))
	{
		puts("AI 助手需要有效的患者登录。");
		return;
	}
	puts("服务器已授权，正在启动本地 AI 助手。");
	if (system(PYTHON_AI_SCRIPT) != 0) puts("无法运行 AI 助手，请检查客户端的 Python 环境。");
}

#endif
