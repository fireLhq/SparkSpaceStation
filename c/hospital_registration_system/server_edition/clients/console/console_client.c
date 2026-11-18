#include "../common/client.h"
#include "../common/client_auth.h"



void patient_menu(void);
void patient_make_appointment(void);
void patient_view_appointments(void);
void patient_view_payments(void);
void patient_view_queue_status(void);
void patient_ai_consultation(void);
void patient_view_rules(void);
void doctor_menu(void);
void doctor_call_patient(void);
void doctor_update_capacity(void);

int main(int argc, char **argv)
{
	if (!client_connect(argc, argv)) return 1;
	for (;;)
	{
		printf("\n====================================\n");
		printf("          医院挂号管理系统主页\n");
		printf("====================================\n");
		printf("[1] 患者登录\n[2] 患者注册\n[3] 医生登录\n[0] 退出系统\n");
		int choice = get_int_in_range(0, 3, "请选择操作: ");
		switch (choice)
		{
			case 1:
				if (client_login(ROLE_PATIENT))
				{
					patient_menu();
					client_logout();
				}
				break;
			case 2:
				patient_register();
				break;
			case 3:
				if (client_login(ROLE_DOCTOR))
				{
					doctor_menu();
					client_logout();
				}
				break;
			default:
				printf("\n感谢使用，系统已退出！\n");
				return 0;
		}
	}
}

void patient_menu()
{
	while (1)
	{
		printf("\n====================================\n");
		printf("           患者服务主菜单           \n");
		printf("====================================\n");
		printf("[1] 在线预约挂号\n");
		printf("[2] 挂号记录查询\n");
		printf("[3] 缴费记录明细\n");
		printf("[4] 实时候诊排队进度\n");
		printf("[5] AI智能问诊助手\n");
		printf("[6] 就诊须知与规则\n");
		printf("[0] 返回上一级\n");
		printf("====================================\n");
		printf("请选择操作: ");

		int choice = get_int_in_range(0, 6, "");
		if (choice < 0) return;

		switch (choice)
		{
			case 1:
				patient_make_appointment();
				break;
			case 2:
				patient_view_appointments();
				break;
			case 3:
				patient_view_payments();
				break;
			case 4:
				patient_view_queue_status();
				break;
			case 5:
				patient_ai_consultation();
				break;
			case 6:
				patient_view_rules();
				break;
			case 0:
				return;
			default:
				printf("无效的选择！\n");
				break;
		}
	}
}

static int select_appointment_date(int doc_idx, int64_t *visit, int *period)
{
	WireDay dates[7];
	if (!fetch_days(doc_idx, dates)) return 0;
	puts("\n--- 未来 7 天号源列表 ---");
	for (int i = 0; i < 7; i++)
	{
		WireDay *day = &dates[i];
		printf("[%d] %04d-%02d-%02d | 上午: %s（余%d）| 下午: %s（余%d）\n",
		       i + 1, day->year, day->month, day->day,
		       day->am_open ? "可选" : "不可选", day->am_left,
		       day->pm_open ? "可选" : "不可选", day->pm_left);
	}
	int day = get_int_in_range(0, 7, "请选择日期 (1-7, 0退出): ");
	if (day <= 0) return 0;
	*period = get_int_in_range(0, 1, "请选择时段 (0-上午, 1-下午): ");
	if (*period < 0) return 0;
	if (*period ? !dates[day - 1].pm_open : !dates[day - 1].am_open)
	{
		puts("该时段不可选。");
		return 0;
	}
	*visit = dates[day - 1].visit;
	return 1;
}

static void handle_unpaid_order_flow(const Appointment *app)
{
	int idx = find_doctor_index(app->doctor_id);
	if (idx < 0) return;
	Doctor *doc = &g_doctors[idx];
	struct tm *day = display_time(app->visit);
	if (!day) return;
	printf("\n挂号单 %lld | %s | %04d-%02d-%02d %s | %.2f 元\n",
	       (long long)app->id, doc->name, day->tm_year + 1900, day->tm_mon + 1, day->tm_mday,
	       app->period ? "下午" : "上午", doc->fee);
	printf("服务器返回的支付剩余时间: %d秒\n", app->seconds_left);
	int op = get_int_in_range(1, 3, "[1] 立即支付 [2] 放弃支付 [3] 稍后支付: ");
	if (op == 1)
		puts(rpc_action(OP_PAYMENT, app->id, 0, 1, 0) ? "支付成功！" : "订单已过期或状态已改变。");
	else if (op == 2)
		puts(rpc_action(OP_PAYMENT, app->id, 0, 2, 0) ? "挂号单已作废。" : "订单已过期或状态已改变。");
	else if (op == 3)
		puts("挂号单已保留。退出登录会作废未支付订单。");
}

void patient_make_appointment()
{
	Appointment app;
	if (fetch_pending(&app))
	{
		handle_unpaid_order_flow(&app);
		return;
	}

	if (g_dept_count == 0)
	{
		printf("\n暂无科室信息！\n");
		return;
	}
	printf("\n--- 科室列表 ---\n");
	for (int i = 0; i < g_dept_count; i++)
		printf("[%d] %s | 位置: %s\n", g_depts[i].id, g_depts[i].name, g_depts[i].location);

	int dept_id = get_int_in_range(0, INT_MAX, "科室编号 (0退出): ");
	if (dept_id <= 0) return;
	int found = 0;
	for (int i = 0; i < g_dept_count; i++) if (g_depts[i].id == dept_id) found = 1;
	if (!found)
	{
		printf("科室编号不存在。\n");
		return;
	}

	int doc_options[MAX_DOCTORS];
	int doc_opt_cnt = 0;
	printf("\n--- 诊区医生 ---\n");
	for (int i = 0; i < g_doctor_count; i++)
	{
		if (g_doctors[i].department_id == dept_id)
		{
			doc_options[doc_opt_cnt++] = i;
			printf("[%d] %s 医师 (%s) | 费用: %.2f元 | 值班: %02d:00-%02d:00\n",
			       doc_opt_cnt,
			       g_doctors[i].name,
			       g_doctors[i].title,
			       g_doctors[i].fee,
			       g_doctors[i].start_work_hour,
			       g_doctors[i].end_work_hour);
		}
	}

	if (doc_opt_cnt == 0)
	{
		printf("该科室暂无出诊医生。\n");
		return;
	}

	int doc_choice = get_int_in_range(0, doc_opt_cnt, "请选择医生序号 (0退出): ");
	if (doc_choice <= 0) return;
	int selected_doc_idx = doc_options[doc_choice - 1];

	int64_t visit;
	int period;
	if (!select_appointment_date(selected_doc_idx, &visit, &period)) return;
	if (book(selected_doc_idx, visit, period, &app)) handle_unpaid_order_flow(&app);
	else puts("时段已截止、名额已满或已有有效订单。");
}

void patient_view_appointments(void)
{
	int count;
	Appointment *apps = (Appointment *)fetch_history(OP_APPOINTMENTS, sizeof(Appointment), &count);
	const char *status[] = {"待支付", "待看诊", "看诊中", "已完成", "已作废"};
	int can_reschedule = 0;
	for (int i = 0; i < count; i++)
	{
		Appointment *app = &apps[i];
		struct tm *day = display_time(app->visit);
		if (!day || app->status < 0 || app->status > 4) continue;
		int eligible = app->status == 4 && app->paid;
		printf("单号:%lld | %04d-%02d-%02d %s | 医生工号:%d | %s%s\n",
		       (long long)app->id, day->tm_year + 1900, day->tm_mon + 1, day->tm_mday,
		       app->period ? "下午" : "上午", app->doctor_id, status[app->status],
		       eligible ? "（可改签）" : "");
		if (eligible) can_reschedule = 1;
	}

	char id_text[32];
	int doc_idx = -1;
	long long id = 0;
	if (!count) puts("暂无挂号记录。");
	if (can_reschedule && read_token("要改签的单号 (0返回): ", id_text, sizeof(id_text)))
	{
		errno = 0;
		if (strspn(id_text, "0123456789") == strlen(id_text))
			id = strtoll(id_text, NULL, 10);
		if (!errno && id > 0)
			for (int i = 0; i < count; i++)
				if (apps[i].id == id && apps[i].status == 4 && apps[i].paid)
					doc_idx = find_doctor_index(apps[i].doctor_id);
	}
	free(apps);
	if (!id) return;
	if (doc_idx < 0)
	{
		puts("未找到可改签的订单或医生。");
		return;
	}
	int64_t visit;
	int period;
	if (select_appointment_date(doc_idx, &visit, &period))
		puts(rpc_action(OP_RESCHEDULE, id, visit, 0, period) ? "改签成功！" :
		     "改签失败：时段已截止、名额已满或已有有效订单。");
}

void patient_view_payments(void)
{
	int count;
	Payment *pays = (Payment *)fetch_history(OP_PAYMENTS, sizeof(Payment), &count);
	for (int i = 0; i < count; i++)
	{
		Payment *pay = &pays[i];
		struct tm *t = display_time(pay->paid);
		if (!t) continue;
		printf("流水号:%lld | 挂号单:%lld | %.2f 元 | %04d-%02d-%02d %02d:%02d\n",
		       (long long)pay->id, (long long)pay->appointment_id, pay->amount, t->tm_year + 1900,
		       t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
	}
	if (!count) puts("暂无缴费记录。");
	free(pays);
}

void patient_view_queue_status(void)
{
	WireQueue queues[MAX_DOCTORS];
	int count = (int)query(OP_QUEUE, 0, queues, sizeof(*queues), MAX_DOCTORS);
	if (!count) puts("当前时段没有正在候诊的挂号。");
	for (int i = 0; i < count; i++)
	{
		WireQueue *row = &queues[i];
		printf("%s | %s | %s医生 | %s | 下班 %02d:00 | 前方 %d 人\n",
		       row->dept_name, row->dept_location, row->doc_name,
		       row->period ? "下午" : "上午", row->end_hour, row->ahead_count);
	}
}

void patient_ai_consultation(void)
{
	start_local_ai();
}

void patient_view_rules(void)
{
	FILE *fp = fopen(FILE_RULES, "r");
	if (!fp)
	{
		puts("未找到客户端共用的就诊须知。");
		return;
	}
	int ch;
	while ((ch = fgetc(fp)) != EOF) putchar(ch);
	putchar('\n');
	fclose(fp);
}

void doctor_menu()
{
	while (1)
	{
		printf("\n====================================\n");
		printf("          医生工作台主菜单          \n");
		printf("====================================\n");
		printf("[1] 呼叫下一位待诊患者\n");
		printf("[2] 修改个人出诊接纳人数\n");
		printf("[0] 返回上一级\n");
		printf("====================================\n");
		printf("请选择操作: ");

		int choice = get_int_in_range(0, 2, "");
		if (choice < 0) return;

		switch (choice)
		{
			case 1:
				doctor_call_patient();
				break;
			case 2:
				doctor_update_capacity();
				break;
			case 0:
				return;
			default:
				printf("无效的选择！\n");
				break;
		}
	}
}

void doctor_call_patient(void)
{
	WireCall call;
	if (query(OP_CALL, 0, &call, sizeof(call), 1) != 1)
	{
		puts("当前没有可叫号的患者。");
		return;
	}
	printf("叫号单 %lld | 患者 %d %s | 性别: %s\n", (long long)call.id, call.patient_id,
	       call.name, call.gender == 0 ? "男" : call.gender == 1 ? "女" : "未知");
	if (call.age >= 0) printf("年龄: %d 岁\n", call.age);
	int op = get_int_in_range(0, 2, "[1] 完成 [2] 未到作废 [0] 返回队列: ");
	puts(rpc_action(OP_FINISH, call.id, 0, op < 0 ? 0 : op, 0) ?
	     "处理成功。" : "该预约已过期或状态已改变。");
}

void doctor_update_capacity(void)
{
	int doc_idx = find_doctor_index(current_account.id);
	if (doc_idx < 0) return;
	Doctor *doc = &g_doctors[doc_idx];
	printf("当前接纳上限 -> 上午: %d, 下午: %d\n", doc->max_am, doc->max_pm);
	int am = get_int_in_range(0, 99, "新的上午上限 (0-99): ");
	if (am < 0) return;
	int pm = get_int_in_range(0, 99, "新的下午上限 (0-99): ");
	if (pm < 0) return;
	puts(save_capacity(doc_idx, am, pm) ? "接纳上限修改成功！" :
	     "新上限低于仍可预约时段的已占人数，修改取消。");
}
