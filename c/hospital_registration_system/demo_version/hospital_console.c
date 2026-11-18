#include "hospital_core.h"
#include "hospital_auth.h"

#define FILE_RULES "resource/rules.txt"


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

int main(void)
{
	init_system();
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
				if (patient_login())
				{
					patient_menu();
					cleanup_patient_unpaid_on_exit(g_current_patient.id);
					memset(&g_current_patient, 0, sizeof(g_current_patient));
				}
				break;
			case 2:
				patient_register();
				break;
			case 3:
				if (doctor_login()) doctor_menu();
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

static int select_appointment_date(int doc_idx, time_t *visit, int *period)
{
	time_t now = time(NULL);
	printf("\n--- 未来 7 天号源列表 ---\n");
	for (int i = 0; i < 7; i++)
	{
		time_t date = day_zero(now, i);
		struct tm day = *localtime(&date);
		printf("[%d] %04d-%02d-%02d | 上午: %s（余%d）| 下午: %s（余%d）\n",
		       i + 1, day.tm_year + 1900, day.tm_mon + 1, day.tm_mday,
		       slot_available(doc_idx, date, 0, now) ? "可选" : "不可选",
		       remaining_slots(doc_idx, date, 0),
		       slot_available(doc_idx, date, 1, now) ? "可选" : "不可选",
		       remaining_slots(doc_idx, date, 1));
	}
	int day = get_int_in_range(0, 7, "请选择日期 (1-7, 0退出): ");
	if (day <= 0) return 0;
	*period = get_int_in_range(0, 1, "请选择时段 (0-上午, 1-下午): ");
	if (*period < 0) return 0;
	*visit = day_zero(now, day - 1);
	return 1;
}

static void handle_unpaid_order_flow(AppointmentNode *node, int doc_idx)
{
	time_t now = time(NULL);
	Doctor *doc = &g_doctors[doc_idx].info;
	if (pending_expired(&node->data, doc, now))
	{
		change_status(doc_idx, node, 4);
		printf("订单已过期，名额已释放。\n");
		return;
	}
	struct tm *day = localtime(&node->data.visit);
	printf("\n挂号单 %lld | %s | %04d-%02d-%02d %s | %.2f 元\n",
	       node->data.id, doc->name, day->tm_year + 1900, day->tm_mon + 1, day->tm_mday,
	       node->data.period ? "下午" : "上午", doc->fee);
	printf("支付剩余时间: %d秒\n", UNPAID_TIMEOUT_SECONDS - (int)difftime(now, node->data.created));
	int op = get_int_in_range(1, 3, "[1] 立即支付 [2] 放弃支付 [3] 稍后支付: ");
	if (op == 1)
		puts(pay_appointment(node, doc_idx) ? "支付成功！已排入候诊队列。" : "订单已过期。");
	else if (op == 2)
	{
		change_status(doc_idx, node, 4);
		puts("挂号单已作废，名额已释放。");
	}
	else if (op == 3)
		puts("挂号单已保留。正常退出患者菜单会作废未支付订单。");
}

void patient_make_appointment()
{
	clean_all_expired_appointments();

	int existing_doc_idx = -1;
	AppointmentNode *unpaid_node = find_unpaid_appointment_in_memory(g_current_patient.id, &existing_doc_idx);
	if (unpaid_node != NULL)
	{
		handle_unpaid_order_flow(unpaid_node, existing_doc_idx);
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
		if (g_doctors[i].info.department_id == dept_id)
		{
			doc_options[doc_opt_cnt++] = i;
			printf("[%d] %s 医师 (%s) | 费用: %.2f元 | 值班: %02d:00-%02d:00\n",
			       doc_opt_cnt,
			       g_doctors[i].info.name,
			       g_doctors[i].info.title,
			       g_doctors[i].info.fee,
			       g_doctors[i].info.start_work_hour,
			       g_doctors[i].info.end_work_hour);
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

	if (has_active_appointment(selected_doc_idx, g_current_patient.id))
	{
		puts("您已持有该医生的有效挂号单。");
		return;
	}
	time_t visit;
	int period;
	if (!select_appointment_date(selected_doc_idx, &visit, &period)) return;
	AppointmentNode *node = create_appointment(selected_doc_idx, visit, period);
	if (node) handle_unpaid_order_flow(node, selected_doc_idx);
	else puts("时段已截止、名额已满或已有有效订单。");
}

void patient_view_appointments(void)
{
	clean_all_expired_appointments();
	int found = 0, can_reschedule = 0;
	const char *status[] = {"待支付", "待看诊", "看诊中", "已完成", "已作废"};
	for (int i = 0; i < g_app_count; i++)
	{
		Appointment *app = &g_apps[i];
		if (app->patient_id != g_current_patient.id) continue;
		struct tm *day = localtime(&app->visit);
		int eligible = app->status == 4 && is_appointment_paid(app->id);
		printf("单号:%lld | %04d-%02d-%02d %s | 医生工号:%d | %s%s\n",
		       app->id, day->tm_year + 1900, day->tm_mon + 1, day->tm_mday,
		       app->period ? "下午" : "上午", app->doctor_id, status[app->status],
		       eligible ? "（可改签）" : "");
		found = 1;
		if (eligible) can_reschedule = 1;
	}
	if (!found) puts("暂无挂号记录。");
	if (!can_reschedule) return;
	char id_text[32];
	if (!read_token("请输入要改签的单号 (0返回): ", id_text, sizeof(id_text))) return;
	if (strspn(id_text, "0123456789") != strlen(id_text))
	{
		puts("单号必须为纯数字。");
		return;
	}
	errno = 0;
	long long id = strtoll(id_text, NULL, 10);
	if (id == 0) return;
	Appointment *target = errno ? NULL : find_appointment(id);
	if (!target || target->patient_id != g_current_patient.id || target->status != 4 ||
	        !is_appointment_paid(id))
	{
		puts("未找到可改签的挂号单。");
		return;
	}
	int doc_idx = find_doctor_index(target->doctor_id);
	if (doc_idx < 0)
	{
		puts("医生信息缺失，无法改签。");
		return;
	}
	time_t visit;
	int period;
	if (select_appointment_date(doc_idx, &visit, &period))
		puts(reschedule_appointment(*target, doc_idx, visit, period) ? "改签成功！" :
		     "改签失败：时段已截止、名额已满或已有该医生的有效订单。");
}

void patient_view_payments(void)
{
	int found = 0;
	for (int i = 0; i < g_payment_count; i++)
	{
		Payment *pay = &g_payments[i];
		Appointment *app = find_appointment(pay->appointment_id);
		if (!app || app->patient_id != g_current_patient.id) continue;
		struct tm *t = localtime(&pay->paid);
		printf("流水号:%lld | 挂号单:%lld | %.2f 元 | %04d-%02d-%02d %02d:%02d\n",
		       pay->id, pay->appointment_id, pay->amount, t->tm_year + 1900,
		       t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
		found = 1;
	}
	if (!found) puts("暂无缴费记录。");
}

void patient_view_queue_status()
{
	clean_all_expired_appointments();

	// 确定当前时段
	time_t now = time(NULL);
	struct tm *now_tm = localtime(&now);
	int cur_period = (now_tm->tm_hour < 12) ? 0 : 1;

	// 收集所有匹配的医生信息
	typedef struct
	{
		char dept_name[32];
		char dept_location[64];
		char doc_name[16];
		int end_hour;
		int ahead_count;
	} QueueInfo;
	QueueInfo queues[MAX_DOCTORS];
	int queue_cnt = 0;

	for (int i = 0; i < g_doctor_count && queue_cnt < MAX_DOCTORS; i++)
	{
		AppointmentNode *curr = g_doctors[i].head;
		int found_me = 0;
		// 先找有没有该患者待诊的该时段挂号
		while (curr)
		{
			if (is_consultation_time(&curr->data, &g_doctors[i].info, now) &&
			        curr->data.status == 1 && curr->data.patient_id == g_current_patient.id)
			{
				found_me = 1;
				break;
			}
			curr = curr->next;
		}
		if (!found_me) continue;

		// 计算前方人数（只计 status==1 且排在前面的）
		int ahead = 0;
		curr = g_doctors[i].head;
		while (curr)
		{
			if (is_consultation_time(&curr->data, &g_doctors[i].info, now) &&
			        curr->data.status == 1)
			{
				if (curr->data.patient_id == g_current_patient.id)
				{
					break;  // 找到自己，停止计数
				}
				ahead++;
			}
			curr = curr->next;
		}

		// 填充信息
		Doctor doc = g_doctors[i].info;
		strcpy(queues[queue_cnt].doc_name, doc.name);
		queues[queue_cnt].end_hour = doc.end_work_hour;
		queues[queue_cnt].ahead_count = ahead;
		// 查找科室
		int found_dept = 0;
		for (int d = 0; d < g_dept_count; d++)
		{
			if (g_depts[d].id == doc.department_id)
			{
				strcpy(queues[queue_cnt].dept_name, g_depts[d].name);
				strcpy(queues[queue_cnt].dept_location, g_depts[d].location);
				found_dept = 1;
				break;
			}
		}
		if (!found_dept)
		{
			strcpy(queues[queue_cnt].dept_name, "未知科室");
			queues[queue_cnt].dept_location[0] = '\0';
		}
		queue_cnt++;
	}

	if (queue_cnt == 0)
	{
		printf("\n您当前没有正在候诊的挂号。\n");
		return;
	}

	printf("\n--- 实时候诊排队进度 ---\n");
	printf("当前时段: %s\n", cur_period == 0 ? "上午" : "下午");
	printf("------------------------------------------------\n");
	for (int i = 0; i < queue_cnt; i++)
	{
		printf("科室: %s\n", queues[i].dept_name);
		printf("地址: %s\n", queues[i].dept_location);
		printf("医生: %s 医师\n", queues[i].doc_name);
		printf("下班时间: %d:00\n", queues[i].end_hour);
		printf("前方排队人数: %d 人\n", queues[i].ahead_count);
		printf("------------------------------------------------\n");
	}
}

void patient_ai_consultation()
{
	printf("\n正在调起外部 AI 医疗助手...\n");
	system(PYTHON_AI_SCRIPT);
}

void patient_view_rules()
{
	printf("\n--- 就诊须知与规则 ---\n");
	FILE *fp = fopen(FILE_RULES, "r");
	if (!fp)
	{
		printf("未找到就诊须知文件 (rules.txt)！\n");
		return;
	}
	int ch;
	while ((ch = fgetc(fp)) != EOF) putchar(ch);
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

void doctor_call_patient()
{
	clean_all_expired_appointments();

	int doc_idx = find_doctor_index(g_current_doctor.id);
	if (doc_idx == -1) return;

	time_t now = time(NULL);
	AppointmentNode *curr = g_doctors[doc_idx].head;
	AppointmentNode *target = NULL;

	while (curr)
	{
		if (curr->data.status == 1 &&
		        is_consultation_time(&curr->data, &g_doctors[doc_idx].info, now))
		{
			target = curr;
			break;
		}
		curr = curr->next;
	}

	if (!target)
	{
		printf("\n今日队列中暂无待诊患者！\n");
		return;
	}

	Patient p;
	memset(&p, 0, sizeof(Patient));
	find_patient_by_id(target->data.patient_id, &p);
	int age = calculate_age(p.birth);

	change_status(doc_idx, target, 2);
	printf("\n>> 当前叫号 <<\n");
	printf("挂号单号: %lld\n", target->data.id);
	printf("患者账号: %ld\n", target->data.patient_id);
	printf("患者姓名: %s\n", p.name);
	printf("性别: %s\n", p.gender == 0 ? "男" : (p.gender == 1 ? "女" : "未知"));
	if (age >= 0) printf("年龄: %d 岁\n", age);
	else puts("年龄: 未知");

	int op = get_int_in_range(0, 2, "请选择操作 (1-完成看诊, 2-未到作废, 0-返回): ");

	if (op == 1)
	{
		change_status(doc_idx, target, 3);
		puts("已完成看诊！号源计入今日完成名额。");
	}
	else if (op == 2)
	{
		change_status(doc_idx, target, 4);
		puts("已作废，名额已释放。");
	}
	else
	{
		int expired = is_expired_at(target->data.visit, target->data.period,
		                            &g_doctors[doc_idx].info, time(NULL));
		change_status(doc_idx, target, expired ? 4 : 1);
	}
}

void doctor_update_capacity(void)
{
	int doc_idx = find_doctor_index(g_current_doctor.id);
	if (doc_idx < 0) return;
	Doctor *doc = &g_doctors[doc_idx].info;
	printf("当前接纳上限 -> 上午: %d, 下午: %d\n", doc->max_am, doc->max_pm);
	int am = get_int_in_range(0, 99, "新的上午上限 (0-99): ");
	if (am < 0) return;
	int pm = get_int_in_range(0, 99, "新的下午上限 (0-99): ");
	if (pm < 0) return;
	puts(save_capacity(doc_idx, am, pm) ? "接纳上限修改成功！" :
	     "新上限低于仍可预约时段的已占人数，修改取消。");
}
