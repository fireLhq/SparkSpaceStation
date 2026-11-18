#include "hospital_core.h"
#include "hospital_auth.h"

#define FILE_RULES "resource/rules.txt"

#include <conio.h>
#include <graphics.h>

#define FILE_MENU_IMG "resource/patient_menu.png"
#define FILE_DOCTOR_MENU_IMG "resource/doctor_menu.png"
#define DIR_DEPT_IMG "resource/departments/"
#define DIR_DOC_IMG "resource/doctors/"

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
int file_exists(const char *path);
void draw_return_btn(void);

static HWND g_console_hwnd = NULL;

static void ensure_console_handle()
{
	if (g_console_hwnd == NULL)
		g_console_hwnd = GetConsoleWindow();
}
static void hide_console()
{
	ensure_console_handle();
	if (g_console_hwnd) ShowWindow(g_console_hwnd, SW_HIDE);
}
static void show_console()
{
	ensure_console_handle();
	if (g_console_hwnd) ShowWindow(g_console_hwnd, SW_SHOW);
}

// 图形辅助函数
void draw_button(int x, int y, int w, int h, const char *text, int bgcolor, int textcolor)
{
	setfillcolor(bgcolor);
	setlinecolor(BLACK);
	fillroundrect(x, y, x + w, y + h, 5, 5);
	settextcolor(textcolor);
	setbkmode(TRANSPARENT);
	settextstyle(16, 0, _T("宋体"));
	int tw = textwidth(text);
	int th = textheight(text);
	outtextxy(x + (w - tw) / 2, y + (h - th) / 2, text);
}

int is_in_rect(int mx, int my, int x, int y, int w, int h)
{
	return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

// 显示简单提示框
void show_message_box_gui(const char *msg)
{
	hide_console();
	initgraph(400, 150);
	setbkcolor(WHITE);
	cleardevice();
	settextcolor(BLACK);
	setbkmode(TRANSPARENT);
	settextstyle(20, 0, _T("宋体"));
	int tw = textwidth(msg);
	int th = textheight(msg);
	outtextxy((400 - tw)/2, (150 - th)/2, msg);
	Sleep(2000);
	closegraph();
	show_console();
}

// 图形确认框
int show_confirm_box_gui(const char *msg)
{
	int win_w = 400, win_h = 200;
	hide_console();
	initgraph(win_w, win_h);
	setbkcolor(WHITE);
	cleardevice();
	settextcolor(BLACK);
	setbkmode(TRANSPARENT);
	settextstyle(16, 0, _T("宋体"));
	int tw = textwidth(msg);
	outtextxy((win_w - tw)/2, 30, msg);

	draw_button(80, 120, 100, 40, "确定", LIGHTGREEN, BLACK);
	draw_button(220, 120, 100, 40, "取消", LIGHTRED, BLACK);

	ExMessage e;
	int choice = 0;
	while (1)
	{
		e = getmessage(EX_MOUSE);
		if (e.message == WM_LBUTTONDOWN)
		{
			if (is_in_rect(e.x, e.y, 80, 120, 100, 40))
			{
				choice = 1;
				break;
			}
			if (is_in_rect(e.x, e.y, 220, 120, 100, 40))
			{
				choice = 0;
				break;
			}
		}
	}
	closegraph();
	show_console();
	return choice;
}

// 图形化日期选择
int select_appointment_date_gui(int doc_idx, time_t *out_visit, int *out_period)
{
	time_t now = time(NULL);
	int cols = 7;
	int rows = 2;
	int cell_w = 80, cell_h = 60;
	int win_w = cols * cell_w + 20;
	int win_h = rows * cell_h + 100;
	if (win_w < 600) win_w = 600;
	if (win_h < 400) win_h = 400;

	hide_console();
	initgraph(win_w, win_h);
	setbkcolor(WHITE);
	cleardevice();
	settextcolor(BLACK);
	setbkmode(TRANSPARENT);
	settextstyle(18, 0, _T("宋体"));

	draw_button(10, 10, 60, 30, "返回", LIGHTGRAY, BLACK);
	outtextxy(100, 15, "请选择看诊日期及时段（点击上午/下午框）");

	int valid[7][2] = {0};
	time_t dates[7];

	for (int i = 0; i < 7; i++)
	{
		time_t t_date = day_zero(now, i);
		dates[i] = t_date;
		int am_valid = slot_available(doc_idx, t_date, 0, now);
		int pm_valid = slot_available(doc_idx, t_date, 1, now);
		valid[i][0] = am_valid;
		valid[i][1] = pm_valid;
	}

	for (int i = 0; i < 7; i++)
	{
		for (int j = 0; j < 2; j++)
		{
			int x = 10 + i * cell_w;
			int y = 50 + j * cell_h;
			setlinecolor(BLACK);
			rectangle(x, y, x + cell_w, y + cell_h);
			int color = valid[i][j] ? LIGHTGREEN : LIGHTGRAY;
			setfillcolor(color);
			fillrectangle(x + 1, y + 1, x + cell_w - 1, y + cell_h - 1);
			struct tm *dt = localtime(&dates[i]);
			if (dt)
			{
				char buf[32];
				snprintf(buf, sizeof(buf), "%02d/%02d", dt->tm_mon + 1, dt->tm_mday);
				settextcolor(BLACK);
				settextstyle(12, 0, _T("宋体"));
				outtextxy(x + 5, y + 5, buf);
				settextstyle(14, 0, _T("宋体"));
				outtextxy(x + 10, y + 25, j == 0 ? "上午" : "下午");
				char remain[16];
				snprintf(remain, sizeof(remain), "余%d", remaining_slots(doc_idx, dates[i], j));
				outtextxy(x + 10, y + 42, remain);
			}
		}
	}

	ExMessage msg;
	int selected = 0;
	while (1)
	{
		msg = getmessage(EX_MOUSE);
		if (msg.message == WM_LBUTTONDOWN)
		{
			if (is_in_rect(msg.x, msg.y, 10, 10, 60, 30))
			{
				selected = 0;
				break;
			}
			for (int i = 0; i < 7; i++)
			{
				for (int j = 0; j < 2; j++)
				{
					int x = 10 + i * cell_w;
					int y = 50 + j * cell_h;
					if (is_in_rect(msg.x, msg.y, x, y, cell_w, cell_h))
					{
						if (valid[i][j])
						{
							*out_visit = dates[i];
							*out_period = j;
							selected = 1;
							goto end_select;
						}
						else
						{
							settextcolor(RED);
							outtextxy(10, win_h - 30, "该时段不可选！");
							Sleep(500);
							setfillcolor(WHITE);
							bar(10, win_h - 40, 300, win_h - 10);
							settextcolor(BLACK);
						}
					}
				}
			}
		}
	}
end_select:
	closegraph();
	show_console();
	return selected;
}

// 图形化支付确认
int show_payment_confirmation_gui(AppointmentNode *node, int doc_idx)
{
	Doctor doc = g_doctors[doc_idx].info;
	struct tm *dt = localtime(&node->data.visit);
	if (!dt) return 3;

	int win_w = 500, win_h = 350;
	hide_console();
	initgraph(win_w, win_h);
	setbkcolor(WHITE);
	cleardevice();
	settextcolor(BLACK);
	setbkmode(TRANSPARENT);
	settextstyle(16, 0, _T("宋体"));

	char line1[100], line2[100], line3[100];
	snprintf(line1, sizeof(line1), "单号: %lld", node->data.id);
	snprintf(line2, sizeof(line2), "医生: %s  时间: %04d-%02d-%02d %s",
	        doc.name, dt->tm_year + 1900, dt->tm_mon + 1, dt->tm_mday,
	        node->data.period == 0 ? "上午" : "下午");
	snprintf(line3, sizeof(line3), "金额: %.2f 元", doc.fee);

	outtextxy(20, 30, line1);
	outtextxy(20, 70, line2);
	outtextxy(20, 110, line3);

	char countdown[32];
	int remaining = UNPAID_TIMEOUT_SECONDS - (int)(time(NULL) - node->data.created);
	if (remaining < 0) remaining = 0;
	snprintf(countdown, sizeof(countdown), "剩余时间: %d秒", remaining);
	outtextxy(win_w - 200, 20, countdown);

	int bw = 100, bh = 40;
	int bx1 = 40, bx2 = 190, bx3 = 340;
	int by = 200;
	draw_button(bx1, by, bw, bh, "立即支付", LIGHTGREEN, BLACK);
	draw_button(bx2, by, bw, bh, "放弃支付", LIGHTRED, BLACK);
	draw_button(bx3, by, bw, bh, "稍后支付", LIGHTBLUE, BLACK);

	ExMessage msg;
	int choice = 0;
	int last_remaining = remaining;
	while (!choice)
	{
		time_t now = time(NULL);
		if (pending_expired(&node->data, &doc, now))
		{
			choice = 2;
			break;
		}
		int now_rem = UNPAID_TIMEOUT_SECONDS - (int)difftime(now, node->data.created);
		if (now_rem != last_remaining)
		{
			last_remaining = now_rem;
			setfillcolor(WHITE);
			bar(win_w - 200, 20, win_w - 20, 45);
			snprintf(countdown, sizeof(countdown), "剩余时间: %d秒", now_rem);
			outtextxy(win_w - 200, 20, countdown);
		}
		if (peekmessage(&msg, EX_MOUSE) && msg.message == WM_LBUTTONDOWN)
		{
			if (is_in_rect(msg.x, msg.y, bx1, by, bw, bh)) choice = 1;
			if (is_in_rect(msg.x, msg.y, bx2, by, bw, bh)) choice = 2;
			if (is_in_rect(msg.x, msg.y, bx3, by, bw, bh)) choice = 3;
		}
		Sleep(50);
	}

	closegraph();
	show_console();
	return choice;
}

// 主函数
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

// 辅助及GUI函数
int file_exists(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (fp)
	{
		fclose(fp);
		return 1;
	}
	return 0;
}

void draw_return_btn()
{
	setlinecolor(RED);
	setfillcolor(LIGHTGRAY);
	fillrectangle(5, 5, 55, 30);
	settextcolor(BLACK);
	setbkmode(TRANSPARENT);
	outtextxy(12, 8, "返回");
}

int select_department_gui()
{
	Department *depts = g_depts;
	int dept_cnt = g_dept_count;

	if (dept_cnt == 0)
	{
		system("cls");
		printf("\n暂无科室信息！\n");
		Sleep(1200);
		return 0;
	}

	int cols = 8;
	int rows = (dept_cnt + cols - 1) / cols;
	int win_w = cols * 90 + 20;
	int win_h = 40 + rows * 120 + 20;
	if (win_w < 240) win_w = 240;
	if (win_h < 180) win_h = 180;

	hide_console();
	initgraph(win_w, win_h);
	setbkcolor(WHITE);
	cleardevice();
	draw_return_btn();

	for (int i = 0; i < dept_cnt; i++)
	{
		int x = 10 + (i % cols) * 90;
		int y = 40 + (i / cols) * 120;
		char img_path[128];
		snprintf(img_path, sizeof(img_path), "%s%d.png", DIR_DEPT_IMG, depts[i].id);

		if (file_exists(img_path))
		{
			IMAGE img;
			loadimage(&img, img_path, 90, 120);
			putimage(x, y, &img);
		}
		else
		{
			setlinecolor(BLACK);
			setfillcolor(LIGHTBLUE);
			fillrectangle(x, y, x+90, y+120);
			settextcolor(BLACK);
			setbkmode(TRANSPARENT);
			char id_buf[16];
			snprintf(id_buf, sizeof(id_buf), "%d", depts[i].id);
			outtextxy(x+5, y+5, id_buf);
			char name_buf[16];
			strncpy(name_buf, depts[i].name, 8);
			name_buf[8] = '\0';
			outtextxy(x+2, y+50, name_buf);
		}
	}

	ExMessage msg;
	int selected_id = 0;
	while (1)
	{
		msg = getmessage(EX_MOUSE);
		if (msg.message == WM_LBUTTONDOWN)
		{
			if (msg.x >= 5 && msg.x <= 55 && msg.y >= 5 && msg.y <= 30)
			{
				selected_id = 0;
				break;
			}
			if (msg.y >= 40)
			{
				int col = (msg.x - 10) / 90;
				int row = (msg.y - 40) / 120;
				int idx = row * cols + col;
				if (idx >= 0 && idx < dept_cnt)
				{
					int x0 = 10 + col * 90;
					int y0 = 40 + row * 120;
					if (msg.x >= x0 && msg.x <= x0+90 && msg.y >= y0 && msg.y <= y0+120)
					{
						selected_id = depts[idx].id;
						break;
					}
				}
			}
		}
	}
	closegraph();
	show_console();
	return selected_id;
}

// 选择医生 GUI
int select_doctor_gui(int dept_id)
{
	int doc_options[MAX_DOCTORS];
	int doc_opt_cnt = 0;
	for (int i = 0; i < g_doctor_count; i++)
	{
		if (g_doctors[i].info.department_id == dept_id)
		{
			doc_options[doc_opt_cnt++] = i;
		}
	}

	if (doc_opt_cnt == 0)
	{
		hide_console();
		initgraph(400, 200);
		setbkcolor(WHITE);
		cleardevice();
		settextcolor(BLACK);
		setbkmode(TRANSPARENT);
		settextstyle(20, 0, _T("宋体"));
		outtextxy(50, 50, "该科室暂无出诊医生");
		draw_button(150, 120, 100, 40, "返回", LIGHTGRAY, BLACK);
		ExMessage msg;
		while (1)
		{
			msg = getmessage(EX_MOUSE);
			if (msg.message == WM_LBUTTONDOWN && is_in_rect(msg.x, msg.y, 150, 120, 100, 40))
			{
				break;
			}
		}
		closegraph();
		show_console();
		return -1;
	}

	int cols = 8;
	int rows = (doc_opt_cnt + cols - 1) / cols;
	int win_w = cols * 90 + 20;
	int win_h = 40 + rows * 120 + 20;
	if (win_w < 240) win_w = 240;
	if (win_h < 180) win_h = 180;

	hide_console();
	initgraph(win_w, win_h);
	setbkcolor(WHITE);
	cleardevice();
	draw_return_btn();

	for (int i = 0; i < doc_opt_cnt; i++)
	{
		int doc_idx = doc_options[i];
		Doctor doc = g_doctors[doc_idx].info;
		int x = 10 + (i % cols) * 90;
		int y = 40 + (i / cols) * 120;
		char img_path[128];
		snprintf(img_path, sizeof(img_path), "%s%d.png", DIR_DOC_IMG, doc.id);

		if (file_exists(img_path))
		{
			IMAGE img;
			loadimage(&img, img_path, 90, 120);
			putimage(x, y, &img);
		}
		else
		{
			setlinecolor(BLACK);
			setfillcolor(LIGHTGREEN);
			fillrectangle(x, y, x+90, y+120);
			settextcolor(BLACK);
			setbkmode(TRANSPARENT);

			settextstyle(14, 0, _T("宋体"));
			char name_gender_buf[32];
			snprintf(name_gender_buf, sizeof(name_gender_buf), "%s %s", doc.name, doc.gender == 0 ? "男" : "女");
			outtextxy(x+2, y+10, name_gender_buf);

			outtextxy(x+2, y+35, doc.title);

			char fee_buf[16];
			snprintf(fee_buf, sizeof(fee_buf), "%.0f元", doc.fee);
			outtextxy(x+2, y+60, fee_buf);

			settextstyle(12, 0, _T("宋体"));
			char end_time_buf[32];
			snprintf(end_time_buf, sizeof(end_time_buf), "%d:00下班", doc.end_work_hour);
			outtextxy(x+2, y+85, end_time_buf);
		}
	}

	ExMessage msg;
	int selected_doc_idx = -1;
	while (1)
	{
		msg = getmessage(EX_MOUSE);
		if (msg.message == WM_LBUTTONDOWN)
		{
			if (msg.x >= 5 && msg.x <= 55 && msg.y >= 5 && msg.y <= 30)
			{
				selected_doc_idx = -1;
				break;
			}
			if (msg.y >= 40)
			{
				int col = (msg.x - 10) / 90;
				int row = (msg.y - 40) / 120;
				int idx = row * cols + col;
				if (idx >= 0 && idx < doc_opt_cnt)
				{
					int x0 = 10 + col * 90;
					int y0 = 40 + row * 120;
					if (msg.x >= x0 && msg.x <= x0+90 && msg.y >= y0 && msg.y <= y0+120)
					{
						selected_doc_idx = doc_options[idx];
						break;
					}
				}
			}
		}
	}
	closegraph();
	show_console();
	return selected_doc_idx;
}

void patient_menu()
{
	ensure_console_handle();

	while (1)
	{
		hide_console();
		initgraph(600, 800);
		if (file_exists(FILE_MENU_IMG))
		{
			static IMAGE menu_img = {0};
			static int loaded = 0;
			if (!loaded)
			{
				loadimage(&menu_img, FILE_MENU_IMG, 600, 800);
				loaded = 1;
			}
			putimage(0, 0, &menu_img);
		}
		else
		{
			setbkcolor(WHITE);
			cleardevice();
			setlinecolor(BLACK);
			line(0, 400, 600, 400);
			line(200, 400, 200, 800);
			line(400, 400, 400, 800);
			line(0, 600, 600, 600);
			settextcolor(BLACK);
			setbkmode(TRANSPARENT);
			outtextxy(250, 180, "患者服务主菜单");
			outtextxy(50, 490, "[1] 在线预约挂号");
			outtextxy(250, 490, "[2] 挂号记录查询");
			outtextxy(450, 490, "[3] 缴费记录明细");
			outtextxy(50, 690, "[4] 实时候诊排队");
			outtextxy(250, 690, "[5] AI智能问诊助手");
			outtextxy(450, 690, "[6] 就诊须知与规则");
		}
		draw_return_btn();

		ExMessage msg;
		int choice = 0;
		while (1)
		{
			msg = getmessage(EX_MOUSE);
			if (msg.message == WM_LBUTTONDOWN)
			{
				if (msg.x >= 5 && msg.x <= 55 && msg.y >= 5 && msg.y <= 30)
				{
					choice = 0;
					break;
				}
				if (msg.y >= 401 && msg.y <= 600)
				{
					if (msg.x >= 0 && msg.x <= 200) choice = 1;
					else if (msg.x > 200 && msg.x <= 400) choice = 2;
					else if (msg.x > 400 && msg.x <= 600) choice = 3;
					if (choice > 0) break;
				}
				else if (msg.y >= 601 && msg.y <= 800)
				{
					if (msg.x >= 0 && msg.x <= 200) choice = 4;
					else if (msg.x > 200 && msg.x <= 400) choice = 5;
					else if (msg.x > 400 && msg.x <= 600) choice = 6;
					if (choice > 0) break;
				}
			}
		}
		closegraph();
		show_console();

		if (choice == 0) return;

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
		}
	}
}

void patient_make_appointment(void)
{
	clean_all_expired_appointments();
	int doc_idx;
	AppointmentNode *node = find_unpaid_appointment_in_memory(g_current_patient.id, &doc_idx);
	if (!node)
	{
		int dept_id = select_department_gui();
		if (!dept_id) return;
		doc_idx = select_doctor_gui(dept_id);
		if (doc_idx < 0) return;
		if (has_active_appointment(doc_idx, g_current_patient.id))
		{
			show_message_box_gui("您已持有该医生的有效挂号单。");
			return;
		}
		time_t visit;
		int period;
		if (!select_appointment_date_gui(doc_idx, &visit, &period)) return;
		node = create_appointment(doc_idx, visit, period);
		if (!node)
		{
			show_message_box_gui("时段已截止、名额已满或已有有效订单。");
			return;
		}
	}
	int choice = show_payment_confirmation_gui(node, doc_idx);
	if (choice == 1)
		show_message_box_gui(pay_appointment(node, doc_idx) ? "支付成功！已排入候诊队列。" :
		                     "订单已过期。");
	else if (choice == 2)
	{
		change_status(doc_idx, node, 4);
		show_message_box_gui("挂号单已作废，名额已释放。");
	}
	else if (choice == 3)
		show_message_box_gui("挂号单已保留，请及时支付。");
}

void patient_view_appointments()
{
	clean_all_expired_appointments();

	int *indices = (int *)malloc((size_t)g_app_count * sizeof(int));
	if (g_app_count && !indices) data_error("out of memory");
	int my_cnt = 0;
	for (int i = 0; i < g_app_count; i++)
		if (g_apps[i].patient_id == g_current_patient.id) indices[my_cnt++] = i;
	if (!my_cnt)
	{
		free(indices);
		show_message_box_gui("暂无挂号记录。");
		return;
	}

	int per_page = 12;
	int total_pages = 1 + (my_cnt - 1) / per_page;
	int current_page = 0;
	int win_w = 960;
	int win_h = 620;

	while (1)
	{
		hide_console();
		initgraph(win_w, win_h);
		setbkcolor(WHITE);
		cleardevice();
		settextcolor(BLACK);
		setbkmode(TRANSPARENT);
		settextstyle(14, 0, _T("宋体"));

		char title[64];
		snprintf(title, sizeof(title), "挂号记录（第%d/%d页）", current_page + 1, total_pages);
		outtextxy(10, 10, title);

		int start = current_page * per_page;
		int end = my_cnt - start > per_page ? start + per_page : my_cnt;
		int y = 40;
		outtextxy(10, y, "单号");
		outtextxy(100, y, "日期");
		outtextxy(190, y, "时段");
		outtextxy(240, y, "科室");
		outtextxy(340, y, "医生");
		outtextxy(460, y, "状态");
		outtextxy(560, y, "下单时间");
		outtextxy(720, y, "操作");
		y += 28;

		int btn_x[12], btn_y[12], btn_index[12];
		int btn_count = 0;

		for (int i = start; i < end; i++)
		{
			Appointment *app = &g_apps[indices[i]];

			// ---------- 修改开始：使用局部结构体复制，避免 localtime 静态缓冲区覆盖 ----------
			struct tm dt_tm, ct_tm;
			struct tm *dt_ptr = localtime(&app->visit);
			if (!dt_ptr) continue;
			dt_tm = *dt_ptr;   // 复制一份

			struct tm *ct_ptr = localtime(&app->created);
			if (!ct_ptr) continue;
			ct_tm = *ct_ptr;   // 复制一份
			// ---------- 修改结束 ----------

			char date_buf[20], time_buf[8], status_buf[16], created_buf[32];
			snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d", dt_tm.tm_year+1900, dt_tm.tm_mon+1, dt_tm.tm_mday);
			snprintf(time_buf, sizeof(time_buf), "%s", app->period == 0 ? "上午" : "下午");
			const char *status_text[] = {"待支付","待看诊","看诊中","已完成","已作废"};
			snprintf(status_buf, sizeof(status_buf), "%s", (app->status>=0&&app->status<=4)?status_text[app->status]:"未知");
			snprintf(created_buf, sizeof(created_buf), "%04d-%02d-%02d %02d:%02d", ct_tm.tm_year+1900, ct_tm.tm_mon+1, ct_tm.tm_mday, ct_tm.tm_hour, ct_tm.tm_min);

			char doc_name[20] = "未知";
			char dept_name[20] = "未知";
			int doc_idx = find_doctor_index(app->doctor_id);
			if (doc_idx != -1)
			{
				strncpy(doc_name, g_doctors[doc_idx].info.name, 15);
				doc_name[15] = '\0';
				int dept_id = g_doctors[doc_idx].info.department_id;
				for (int d = 0; d < g_dept_count; d++)
				{
					if (g_depts[d].id == dept_id)
					{
						strncpy(dept_name, g_depts[d].name, 15);
						dept_name[15] = '\0';
						break;
					}
				}
			}

			char buf[32];
			snprintf(buf, sizeof(buf), "%lld", app->id);
			outtextxy(10, y, buf);
			outtextxy(100, y, date_buf);
			outtextxy(190, y, time_buf);
			outtextxy(240, y, dept_name);
			outtextxy(340, y, doc_name);
			outtextxy(460, y, status_buf);
			outtextxy(560, y, created_buf);

			int paid = is_appointment_paid(app->id);
			int can_reset = (app->status == 4 && paid);
			if (can_reset)
			{
				draw_button(720, y-5, 60, 25, "改签", LIGHTBLUE, BLACK);
				btn_x[btn_count] = 720;
				btn_y[btn_count] = y-5;
				btn_index[btn_count++] = indices[i];
			}
			y += 33;
		}

		if (current_page > 0) draw_button(10, win_h-40, 60, 30, "上一页", LIGHTGRAY, BLACK);
		if (current_page < total_pages-1) draw_button(80, win_h-40, 60, 30, "下一页", LIGHTGRAY, BLACK);
		draw_button(win_w-70, win_h-40, 60, 30, "返回", LIGHTRED, BLACK);

		ExMessage msg;
		int action = 0;
		while (1)
		{
			msg = getmessage(EX_MOUSE);
			if (msg.message == WM_LBUTTONDOWN)
			{
				if (is_in_rect(msg.x, msg.y, win_w-70, win_h-40, 60, 30))
				{
					closegraph();
					show_console();
					free(indices);
					return;
				}
				if (current_page > 0 && is_in_rect(msg.x, msg.y, 10, win_h-40, 60, 30))
				{
					current_page--;
					action = 1;
					break;
				}
				if (current_page < total_pages-1 && is_in_rect(msg.x, msg.y, 80, win_h-40, 60, 30))
				{
					current_page++;
					action = 1;
					break;
				}
				for (int k = 0; k < btn_count; k++)
				{
					if (is_in_rect(msg.x, msg.y, btn_x[k], btn_y[k], 60, 25))
					{
						closegraph();
						show_console();
						Appointment app = g_apps[btn_index[k]];
						int doc_idx = find_doctor_index(app.doctor_id);
						if (doc_idx < 0)
							show_message_box_gui("医生信息缺失，无法改签。");
						else
						{
							time_t date;
							int period;
							if (select_appointment_date_gui(doc_idx, &date, &period))
								show_message_box_gui(reschedule_appointment(app, doc_idx, date, period) ?
								                     "改签成功！" : "时段已截止、名额已满或已持有该医生的有效单。");
						}
						goto reload;
					}
				}
			}
		}
		closegraph();
		show_console();
		if (action == 1) continue;
		break;
reload:
		continue;
	}
	free(indices);
}

void patient_view_payments()
{
	int *indices = (int *)malloc((size_t)g_payment_count * sizeof(int));
	if (g_payment_count && !indices) data_error("out of memory");
	int my_cnt = 0;
	for (int i = 0; i < g_payment_count; i++)
	{
		Appointment *app = find_appointment(g_payments[i].appointment_id);
		if (app && app->patient_id == g_current_patient.id) indices[my_cnt++] = i;
	}
	if (!my_cnt)
	{
		free(indices);
		show_message_box_gui("暂无缴费记录。");
		return;
	}

	int per_page = 10;
	int total_pages = 1 + (my_cnt - 1) / per_page;
	int current_page = 0;
	int win_w = 700, win_h = 500;

	while (1)
	{
		hide_console();
		initgraph(win_w, win_h);
		setbkcolor(WHITE);
		cleardevice();
		settextcolor(BLACK);
		setbkmode(TRANSPARENT);
		settextstyle(14, 0, _T("宋体"));

		char title[64];
		snprintf(title, sizeof(title), "缴费记录（第%d/%d页）", current_page + 1, total_pages);
		outtextxy(10, 10, title);

		int start = current_page * per_page;
		int end = my_cnt - start > per_page ? start + per_page : my_cnt;
		int y = 50;
		outtextxy(10, y, "流水号");
		outtextxy(150, y, "挂号单号");
		outtextxy(300, y, "金额");
		outtextxy(450, y, "支付时间");
		y += 25;

		for (int i = start; i < end; i++)
		{
			Payment *pay = &g_payments[indices[i]];
			struct tm *pt = localtime(&pay->paid);
			if (!pt) continue;
			char time_buf[32];
			snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02d %02d:%02d", pt->tm_year+1900, pt->tm_mon+1, pt->tm_mday, pt->tm_hour, pt->tm_min);
			char buf[32];
			snprintf(buf, sizeof(buf), "%lld", pay->id);
			outtextxy(10, y, buf);
			snprintf(buf, sizeof(buf), "%lld", pay->appointment_id);
			outtextxy(150, y, buf);
			snprintf(buf, sizeof(buf), "%.2f", pay->amount);
			outtextxy(300, y, buf);
			outtextxy(450, y, time_buf);
			y += 30;
		}

		if (current_page > 0) draw_button(10, win_h-40, 60, 30, "上一页", LIGHTGRAY, BLACK);
		if (current_page < total_pages-1) draw_button(80, win_h-40, 60, 30, "下一页", LIGHTGRAY, BLACK);
		draw_button(win_w-70, win_h-40, 60, 30, "返回", LIGHTRED, BLACK);

		ExMessage msg;
		int action = 0;
		while (1)
		{
			msg = getmessage(EX_MOUSE);
			if (msg.message == WM_LBUTTONDOWN)
			{
				if (is_in_rect(msg.x, msg.y, win_w-70, win_h-40, 60, 30))
				{
					closegraph();
					show_console();
					free(indices);
					return;
				}
				if (current_page > 0 && is_in_rect(msg.x, msg.y, 10, win_h-40, 60, 30))
				{
					current_page--;
					action = 1;
					break;
				}
				if (current_page < total_pages-1 && is_in_rect(msg.x, msg.y, 80, win_h-40, 60, 30))
				{
					current_page++;
					action = 1;
					break;
				}
			}
		}
		closegraph();
		show_console();
		if (action == 1) continue;
		break;
	}
	free(indices);
}

void patient_view_queue_status()
{
	clean_all_expired_appointments();

	time_t now = time(NULL);
	struct tm *now_tm = localtime(&now);
	if (!now_tm)
	{
		show_message_box_gui("获取时间失败");
		return;
	}


	typedef struct
	{
		char dept_name[32];
		char dept_location[64];
		char doc_name[16];
		int end_hour;
		int ahead_count;
	} QueueInfo;

	QueueInfo queues[MAX_DOCTORS];  // 修复：使用 MAX_DOCTORS 防止越界
	int queue_cnt = 0;

	for (int i = 0; i < g_doctor_count && queue_cnt < MAX_DOCTORS; i++)
	{
		AppointmentNode *curr = g_doctors[i].head;
		int found = 0;
		while (curr)
		{
			if (is_consultation_time(&curr->data, &g_doctors[i].info, now) &&
			        curr->data.status == 1 &&
			        curr->data.patient_id == g_current_patient.id)
			{
				found = 1;
				break;
			}
			curr = curr->next;
		}
		if (found)
		{
			int ahead = 0;
			curr = g_doctors[i].head;
			while (curr)
			{
				if (is_consultation_time(&curr->data, &g_doctors[i].info, now) &&
				        curr->data.status == 1)
				{
					if (curr->data.patient_id == g_current_patient.id)
					{
						break;
					}
					ahead++;
				}
				curr = curr->next;
			}
			Doctor doc = g_doctors[i].info;
			strcpy(queues[queue_cnt].doc_name, doc.name);
			queues[queue_cnt].end_hour = doc.end_work_hour;
			queues[queue_cnt].ahead_count = ahead;
			int dept_id = doc.department_id;
			int found_dept = 0;
			for (int d = 0; d < g_dept_count; d++)
			{
				if (g_depts[d].id == dept_id)
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
				strcpy(queues[queue_cnt].dept_location, "");
			}
			queue_cnt++;
		}
	}

	if (queue_cnt == 0)
	{
		hide_console();
		initgraph(400, 200);
		setbkcolor(WHITE);
		cleardevice();
		settextcolor(BLACK);
		setbkmode(TRANSPARENT);
		settextstyle(18, 0, _T("宋体"));
		outtextxy(50, 80, "您当前没有正在候诊的挂号");
		draw_button(150, 140, 80, 30, "返回", LIGHTGRAY, BLACK);
		ExMessage msg;
		while (1)
		{
			msg = getmessage(EX_MOUSE);
			if (msg.message == WM_LBUTTONDOWN && is_in_rect(msg.x, msg.y, 150, 140, 80, 30))
				break;
		}
		closegraph();
		show_console();
		return;
	}

	int win_w = 700, win_h = 100 + queue_cnt * 40 + 60;
	if (win_h < 300) win_h = 300;
	if (win_h > 600) win_h = 600;
	hide_console();
	initgraph(win_w, win_h);
	setbkcolor(WHITE);
	cleardevice();
	settextcolor(BLACK);
	setbkmode(TRANSPARENT);
	settextstyle(14, 0, _T("宋体"));

	outtextxy(10, 10, "实时候诊排队进度");
	int y = 40;
	outtextxy(10, y, "科室");
	outtextxy(150, y, "地址");
	outtextxy(300, y, "医生");
	outtextxy(420, y, "下班时间");
	outtextxy(530, y, "前方人数");
	y += 25;

	for (int i = 0; i < queue_cnt; i++)
	{
		char buf[32];
		outtextxy(10, y, queues[i].dept_name);
		outtextxy(150, y, queues[i].dept_location);
		outtextxy(300, y, queues[i].doc_name);
		snprintf(buf, sizeof(buf), "%d:00", queues[i].end_hour);
		outtextxy(420, y, buf);
		snprintf(buf, sizeof(buf), "%d 人", queues[i].ahead_count);
		outtextxy(530, y, buf);
		y += 35;
	}

	draw_button(win_w-70, win_h-40, 60, 30, "返回", LIGHTGRAY, BLACK);
	ExMessage msg;
	while (1)
	{
		msg = getmessage(EX_MOUSE);
		if (msg.message == WM_LBUTTONDOWN && is_in_rect(msg.x, msg.y, win_w-70, win_h-40, 60, 30))
			break;
	}
	closegraph();
	show_console();
}

void patient_ai_consultation()
{
	system("cls");
	printf("正在调起外部 AI 医疗助手...\n");
	system(PYTHON_AI_SCRIPT);
	system("pause");
}

void patient_view_rules()
{
	FILE *fp = fopen(FILE_RULES, "r");
	if (!fp)
	{
		hide_console();
		initgraph(400, 200);
		setbkcolor(WHITE);
		cleardevice();
		settextcolor(BLACK);
		setbkmode(TRANSPARENT);
		settextstyle(20, 0, _T("宋体"));
		outtextxy(50, 80, "未找到 rules.txt");
		getch();
		closegraph();
		show_console();
		return;
	}
	char content[4096] = {0};
	int len = 0;
	int ch;
	while ((ch = fgetc(fp)) != EOF && len < 4095) content[len++] = (char)ch;
	content[len] = '\0';
	fclose(fp);

	hide_console();
	initgraph(600, 500);
	setbkcolor(WHITE);
	cleardevice();
	settextcolor(BLACK);
	setbkmode(TRANSPARENT);
	settextstyle(14, 0, _T("宋体"));

	int y = 20;
	char *line = strtok(content, "\n");
	while (line && y < 460)
	{
		outtextxy(10, y, line);
		y += 24;
		line = strtok(NULL, "\n");
	}
	draw_button(500, 450, 60, 30, "返回", LIGHTGRAY, BLACK);

	ExMessage msg;
	while (1)
	{
		msg = getmessage(EX_MOUSE);
		if (msg.message == WM_LBUTTONDOWN && is_in_rect(msg.x, msg.y, 500, 450, 60, 30))
			break;
	}
	closegraph();
	show_console();
}

// 医生菜单 GUI 界面
void doctor_menu()
{
	ensure_console_handle();

	while (1)
	{
		hide_console();
		initgraph(600, 700);

		if (file_exists(FILE_DOCTOR_MENU_IMG))
		{
			static IMAGE doc_menu_img = {0};
			static int loaded = 0;
			if (!loaded)
			{
				loadimage(&doc_menu_img, FILE_DOCTOR_MENU_IMG, 600, 700);
				loaded = 1;
			}
			putimage(0, 0, &doc_menu_img);
		}
		else
		{
			setbkcolor(WHITE);
			cleardevice();

			setfillcolor(LIGHTBLUE);
			solidrectangle(0, 0, 600, 400);
			settextcolor(WHITE);
			setbkmode(TRANSPARENT);
			settextstyle(32, 0, _T("宋体"));
			outtextxy(220, 180, "医生工作台");

			setfillcolor(LIGHTCYAN);
			fillrectangle(0, 401, 300, 700);
			setlinecolor(BLACK);
			rectangle(0, 401, 300, 700);
			settextcolor(BLACK);
			settextstyle(20, 0, _T("宋体"));
			outtextxy(60, 540, "呼叫下一位待诊患者");

			setfillcolor(LIGHTGREEN);
			fillrectangle(301, 401, 600, 700);
			rectangle(301, 401, 600, 700);
			outtextxy(360, 540, "修改个人出诊接纳人数");
		}
		draw_return_btn();

		ExMessage msg;
		int choice = -1;
		while (1)
		{
			msg = getmessage(EX_MOUSE);
			if (msg.message == WM_LBUTTONDOWN)
			{
				if (is_in_rect(msg.x, msg.y, 5, 5, 50, 25))
				{
					choice = 0;
					break;
				}
				if (msg.y >= 401 && msg.y <= 700)
				{
					if (msg.x >= 0 && msg.x <= 300)
					{
						choice = 1;
						break;
					}
					else if (msg.x > 300 && msg.x <= 600)
					{
						choice = 2;
						break;
					}
				}
			}
		}
		closegraph();
		show_console();

		if (choice == 0) return;
		if (choice == 1) doctor_call_patient();
		if (choice == 2) doctor_update_capacity();
	}
}

// 呼叫患者图形化提示框 (修复：移除左上角返回按钮)
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

	int win_w = 500, win_h = 360;
	hide_console();
	initgraph(win_w, win_h);
	setbkcolor(WHITE);
	cleardevice();
	// 不再绘制左上角返回按钮

	if (!target)
	{
		settextcolor(BLACK);
		setbkmode(TRANSPARENT);
		settextstyle(20, 0, _T("宋体"));
		outtextxy(120, 130, "今日队列中暂无待诊患者！");
		draw_button(200, 240, 100, 40, "确定", LIGHTGRAY, BLACK);

		ExMessage msg;
		while (1)
		{
			msg = getmessage(EX_MOUSE);
			if (msg.message == WM_LBUTTONDOWN)
			{
				if (is_in_rect(msg.x, msg.y, 200, 240, 100, 40))
					break;
			}
		}
		closegraph();
		show_console();
		return;
	}

	Patient p;
	memset(&p, 0, sizeof(Patient));
	find_patient_by_id(target->data.patient_id, &p);
	int age = calculate_age(p.birth);

	change_status(doc_idx, target, 2);

	settextcolor(BLACK);
	setbkmode(TRANSPARENT);
	settextstyle(20, 0, _T("宋体"));
	outtextxy(160, 20, "--- 当前叫号单 ---");

	settextstyle(16, 0, _T("宋体"));
	char buf[128];
	snprintf(buf, sizeof(buf), "叫号单号：%lld", target->data.id);
	outtextxy(50, 70, buf);

	snprintf(buf, sizeof(buf), "患者账号：%ld", target->data.patient_id);
	outtextxy(50, 110, buf);

	snprintf(buf, sizeof(buf), "患者姓名：%s", strlen(p.name) > 0 ? p.name : "未知");
	outtextxy(50, 150, buf);

	snprintf(buf, sizeof(buf), "患者性别：%s", p.gender == 0 ? "男" : (p.gender == 1 ? "女" : "未知"));
	outtextxy(50, 190, buf);

	if (age < 0) strcpy(buf, "患者年龄：未知");
	else snprintf(buf, sizeof(buf), "患者年龄：%d 岁", age);
	outtextxy(50, 230, buf);

	draw_button(50, 290, 110, 40, "完成看诊", LIGHTGREEN, BLACK);
	draw_button(195, 290, 110, 40, "未到作废", LIGHTRED, BLACK);
	draw_button(340, 290, 110, 40, "返回", LIGHTGRAY, BLACK);

	ExMessage msg;
	int choice = 0;
	while (1)
	{
		msg = getmessage(EX_MOUSE);
		if (msg.message == WM_LBUTTONDOWN)
		{
			// 仅处理下方按钮
			if (is_in_rect(msg.x, msg.y, 340, 290, 110, 40))
			{
				choice = 0;
				break;
			}
			if (is_in_rect(msg.x, msg.y, 50, 290, 110, 40))
			{
				choice = 1;
				break;
			}
			if (is_in_rect(msg.x, msg.y, 195, 290, 110, 40))
			{
				choice = 2;
				break;
			}
		}
	}

	closegraph();
	show_console();
	if (choice == 1)
	{
		change_status(doc_idx, target, 3);
		show_message_box_gui("已完成看诊！号源计入今日完成名额。");
	}
	else if (choice == 2)
	{
		change_status(doc_idx, target, 4);
		show_message_box_gui("号源已作废，名额已释放。");
	}
	else
	{
		int expired = is_expired_at(target->data.visit, target->data.period,
		                            &g_doctors[doc_idx].info, time(NULL));
		change_status(doc_idx, target, expired ? 4 : 1);
	}
}

void doctor_update_capacity()
{
	int doc_idx = find_doctor_index(g_current_doctor.id);
	if (doc_idx == -1) return;

	int sel_am = g_doctors[doc_idx].info.max_am;
	int sel_pm = g_doctors[doc_idx].info.max_pm;

	int win_w = 820, win_h = 520;
	hide_console();
	initgraph(win_w, win_h);
	BeginBatchDraw();

	int block_w = 37, block_h = 29;
	int gap = 3;
	int start_x = 10, start_y_am = 64, start_y_pm = 263;

	while (1)
	{
		setbkcolor(WHITE);
		cleardevice();
		draw_return_btn();

		settextcolor(BLACK);
		setbkmode(TRANSPARENT);
		settextstyle(18, 0, _T("宋体"));

		char title_buf[128];
		snprintf(title_buf, sizeof(title_buf), "%s 医师 - 修改接纳人数 (当前: 上午%d人, 下午%d人)",
		        g_doctors[doc_idx].info.name, g_doctors[doc_idx].info.max_am, g_doctors[doc_idx].info.max_pm);
		outtextxy(70, 10, title_buf);

		settextstyle(14, 0, _T("宋体"));
		outtextxy(start_x, start_y_am - 22, "【上午接纳人数设置】");
		outtextxy(start_x, start_y_pm - 22, "【下午接纳人数设置】");

		for (int i = 0; i < 100; i++)
		{
			int r = i / 20;
			int c = i % 20;
			int bx = start_x + c * (block_w + gap);
			int by = start_y_am + r * (block_h + gap);

			int is_selected = (i == sel_am);
			setfillcolor(is_selected ? LIGHTRED : LIGHTCYAN);
			setlinecolor(is_selected ? RED : BLACK);
			fillrectangle(bx, by, bx + block_w, by + block_h);

			settextcolor(is_selected ? WHITE : BLACK);
			settextstyle(12, 0, _T("宋体"));
			

			char num_str[16];
			snprintf(num_str, sizeof(num_str), "%d人", i);
			int tw = textwidth(num_str);
			outtextxy(bx + (block_w - tw)/2, by + 7, num_str);
		}

		for (int i = 0; i < 100; i++)
		{
			int r = i / 20;
			int c = i % 20;
			int bx = start_x + c * (block_w + gap);
			int by = start_y_pm + r * (block_h + gap);

			int is_selected = (i == sel_pm);
			setfillcolor(is_selected ? RED : LIGHTGREEN);
			setlinecolor(is_selected ? RED : BLACK);
			fillrectangle(bx, by, bx + block_w, by + block_h);

			settextcolor(is_selected ? WHITE : BLACK);
			settextstyle(12, 0, _T("宋体"));
			

			char num_str[16];
			snprintf(num_str, sizeof(num_str), "%d人", i);
			int tw = textwidth(num_str);
			outtextxy(bx + (block_w - tw)/2, by + 7, num_str);
		}

		draw_button(230, 460, 140, 40, "保存修改", LIGHTBLUE, BLACK);
		FlushBatchDraw();

		ExMessage msg;
		while (1)
		{
			msg = getmessage(EX_MOUSE);
			if (msg.message == WM_LBUTTONDOWN)
			{
				break;
			}
		}

		if (is_in_rect(msg.x, msg.y, 5, 5, 50, 25))
		{
			break;
		}
		if (is_in_rect(msg.x, msg.y, 230, 460, 140, 40))
		{
			int ok = save_capacity(doc_idx, sel_am, sel_pm);
			EndBatchDraw();
			closegraph();
			show_console();
			show_message_box_gui(ok ? "接纳上限修改成功！" : "新上限低于已占人数，修改取消。");
			show_console();
			return;
		}

		for (int i = 0; i < 100; i++)
		{
			int r = i / 20;
			int c = i % 20;
			int bx = start_x + c * (block_w + gap);
			int by = start_y_am + r * (block_h + gap);
			if (is_in_rect(msg.x, msg.y, bx, by, block_w, block_h))
			{
				sel_am = i;
				break;
			}
		}

		for (int i = 0; i < 100; i++)
		{
			int r = i / 20;
			int c = i % 20;
			int bx = start_x + c * (block_w + gap);
			int by = start_y_pm + r * (block_h + gap);
			if (is_in_rect(msg.x, msg.y, bx, by, block_w, block_h))
			{
				sel_pm = i;
				break;
			}
		}
	}

	EndBatchDraw();
	closegraph();
	show_console();
}

// 核心算法函数
