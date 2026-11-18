#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <stdbool.h>
#include <string.h>
#include <conio.h>
#include <time.h>
#include <limits.h>
#include <errno.h>

//用户
typedef struct
{
	char account[10]; //账号
	char password[7]; //密码
	char name[16]; //姓名
	int status; //状态(0-冻结，1-正常)
} User;

//余额
typedef struct
{
	char account[10]; //账号
	long long cny; //人民币(分)
	long long usd; //美元(分)
	long long eur; //欧元(分)
} Balance;

//账单
typedef struct
{
	char account[10]; //账号
	int type; //类型(0-存款，1-取款，2-转入，3-转出，4-购入，5-消费)
	long long amount; //金额
	char unit[4]; //单位
	time_t time; //时间
	char remarks[65]; //备注
} Bill;

//预约
typedef struct
{
	char account[10]; //账号
	long long amount; //金额
	char unit[4]; //单位
	int type; //类型(0-存款，1-取款，2-转账，3-购汇)
	int status; //状态(0-未受理，1-已受理)
	time_t date; //预约日期
	time_t time; //操作时间
} Appointment;

//操作
typedef struct
{
	char account[10]; //账号
	int type; //类型(0-登录，1-改密，2-冻结)
	time_t time; //时间
} Operation;

//FAQ
typedef struct
{
	int id; //编号
	char question[128]; //问题
	char answer[512]; //解答
} FAQItem;

//页面渲染定位
typedef struct
{
	int item_idx; //条目索引
	int char_offset; //条目内部字符串的字节偏移
} PagePos;

//定义全局变量
char lang = '0', current_account[10]; //语言，当前账户
const int RATE_CNY = 100; //人民币(相对人民币)汇率
const int RATE_USD = 720; //美元(相对人民币)汇率
const int RATE_EUR = 780; //欧元(相对人民币)汇率
COORD pos; //位置

//函数声明
//功能函数
void init();
void login();
void menu();
void balance_inquiry();
void deposit();
void withdraw();
void transfer();
void forex();
void transaction_record();
void apply();
void schedule_inquiry();
void reset_password();
void freeze_account();
void ai_assistant();
void manual();
void quit();

//工具函数
int get_char_bytes(const unsigned char *str);
void get_mask_password(char *out_pwd, int max_len);
void get_numeric_input(char *out_str, int max_len, int start_x, int start_y);
void get_decimal_input(char *out_str, int max_len, int start_x, int start_y);
void get_name_input(char *out_str, int max_width, int start_x, int start_y);
bool is_all_digits(const char *str);
bool is_leap_year(int year);
bool is_valid_date(int year, int month, int day);

//视图函数
void cursor(int x, int y);
void clear(int y, int length);
void print_random(int x, int y, const char *str);
void print_center(int row, const char *str);
void print_fixed(const char *str);
PagePos render_items_page(char items[][128], int item_count, PagePos start_pos);
void page();
bool replace_data_file(FILE *source, FILE *temp, const char *temp_name, const char *name, bool read_complete);
int read_balance(FILE *file, Balance *balance);
int read_user(FILE *file, User *user);

//函数实现
//主函数
int main()
{
	SetConsoleOutputCP(CP_UTF8);
	setvbuf(stdout, NULL, _IONBF, 0);
	page();
	init();
	login();
	menu();
	return 0;
}

// 返回 1 表示读到一条记录，0 表示文件结束，-1 表示格式错误或读取失败。
int read_balance(FILE *file, Balance *balance)
{
	char line[96], cny[20], usd[20], eur[20], extra;
	if(fgets(line, sizeof(line), file) == NULL) return feof(file) ? 0 : -1;
	if(sscanf(line, "%9[0-9]%*1[ \t] %19[0-9]%*1[ \t] %19[0-9]%*1[ \t] %19[0-9] %c",
	          balance->account, cny, usd, eur, &extra) != 4 ||
	   strlen(balance->account) != 9 || (strchr(line, '\n') == NULL && !feof(file))) return -1;
	errno = 0;
	balance->cny = strtoll(cny, NULL, 10);
	balance->usd = strtoll(usd, NULL, 10);
	balance->eur = strtoll(eur, NULL, 10);
	return errno == ERANGE ? -1 : 1;
}

int read_user(FILE *file, User *user)
{
	char line[64], status[2], extra;
	if(fgets(line, sizeof(line), file) == NULL) return feof(file) ? 0 : -1;
	if(sscanf(line, "%9[0-9]%*1[ \t] %6[0-9]%*1[ \t] %15s%*1[ \t] %1[01] %c",
	          user->account, user->password, user->name, status, &extra) != 4 ||
	   strlen(user->account) != 9 || strlen(user->password) != 6 ||
	   (strchr(line, '\n') == NULL && !feof(file))) return -1;
	user->status = status[0] - '0';
	return 1;
}

bool replace_data_file(FILE *source, FILE *temp, const char *temp_name, const char *name, bool read_complete)
{
	bool complete = read_complete && feof(source) && !ferror(source) && !ferror(temp);
	fclose(source);
	if(fclose(temp) != 0) complete = false;
	if(complete && MoveFileExA(temp_name, name, MOVEFILE_REPLACE_EXISTING)) return true;
	remove(temp_name);
	clear(3, 14);
	(lang == '1') ? print_center(8, "无法保存数据") : print_center(8, "Cannot save data");
	Sleep(1000);
	return false;
}

//功能函数
//初始化
void init()
{
	while(lang != '1' && lang != '2')
	{
		cursor(2, 15);
		lang = getch();
		clear(15, 1);
	}
	(lang == '1') ? print_fixed("正在加载 ") : print_fixed("Loading. ");
	for(int i = 0; i < 18; i++)
	{
		printf("#");
		Sleep(25);
	}
}

//登录
void login()
{
	char account[10], password[7];
	bool flag, frozen;
	User user;
	FILE *fuser = fopen("user.txt", "r");
	FILE *foperation = fopen("operation.txt", "a");
	if(fuser == NULL || foperation == NULL)
	{
		if(fuser) fclose(fuser);
		if(foperation) fclose(foperation);
		clear(3, 14);
		(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
		cursor(0, 20);
		exit(1);
	}
	for(int i = 0; i < 3; i++)
	{
		flag = false;
		frozen = false;
		clear(3, 14);
		(lang == '1') ? print_center(5, "准备登录您的账户") : print_center(5, "Prepare to log in");
		(lang == '1') ? print_center(8, "请输入账号和密码") : print_center(8, "Input account and password");
		(lang == '1') ? print_random(8, 10, "账号：") : print_random(7, 10, "Account: ");
		(lang == '1') ? get_numeric_input(account, 9, 14, 10) : get_numeric_input(account, 9, 16, 10);
		(lang == '1') ? print_random(10, 12, "密码：") : print_random(8, 12, "Password: ");
		get_mask_password(password, 6);
		rewind(fuser);
		while(read_user(fuser, &user) > 0)
		{
			if(strlen(account) == 9 && strlen(password) == 6 && is_all_digits(password) &&
			   strcmp(account, user.account) == 0 && strcmp(password, user.password) == 0)
			{
				if(user.status == 0)
				{
					frozen = true;
				}
				else
				{
					flag = true;
					strcpy(current_account, user.account);
					fprintf(foperation, "%s %d %lld\n", user.account, 0, (long long)time(NULL));
				}
				break;
			}
		}
		if(flag)
		{
			break;
		}
		else
		{
			if(frozen)
			{
				(lang == '1') ? print_fixed("该账户已被冻结！") : print_fixed("Account has been frozen!");
				Sleep(1500);
				break;
			}
			if(i < 2)
			{
				(lang == '1') ? print_fixed("账号或密码错误，请重新输入！") : print_fixed("Error, please try again!");
				Sleep(1500);
			}
		}
	}
	fclose(fuser);
	fclose(foperation);
	clear(15, 1);
	if(flag)
	{
		FILE *fbalance = fopen("balance.txt", "a");
		if(fbalance == NULL || fclose(fbalance) != 0)
		{
			(lang == '1') ? print_fixed("无法初始化余额文件") : print_fixed("Cannot open balance file");
			cursor(0, 20);
			exit(1);
		}
		(lang == '1') ? print_fixed("登录成功！") : print_fixed("Login successful!");
		Sleep(1000);
	}
	else
	{
		(lang == '1') ? print_fixed("登录失败！") : print_fixed("Login failed!");
		cursor(0, 20);
		exit(0);
	}
}

//菜单
void menu()
{
	char option_str[3];
	int option;
	while(1)
	{
		clear(3, 14);
		(lang == '1') ? print_center(4, "菜单选项栏") : print_center(4, "Menu options bar");
		cursor(3, 6);
		(lang == '1') ? printf("01.余额查询") : printf("01.Balance");
		cursor(3, 7);
		(lang == '1') ? printf("02.存款") : printf("02.Deposit");
		cursor(3, 8);
		(lang == '1') ? printf("03.取款") : printf("03.Withdraw");
		cursor(3, 9);
		(lang == '1') ? printf("04.转账") : printf("04.Transfer");
		cursor(3, 10);
		(lang == '1') ? printf("05.外汇") : printf("05.Forex");
		cursor(3, 11);
		(lang == '1') ? printf("06.交易记录") : printf("06.Record");
		cursor(18, 6);
		(lang == '1') ? printf("07.大额申请") : printf("07.Apply");
		cursor(18, 7);
		(lang == '1') ? printf("08.预约查询") : printf("08.Schedule");
		cursor(18, 8);
		(lang == '1') ? printf("09.修改密码") : printf("09.Resetpwd");
		cursor(18, 9);
		(lang == '1') ? printf("10.账户冻结") : printf("10.Freeze");
		cursor(18, 10);
		(lang == '1') ? printf("11.人工智能") : printf("11.AI");
		cursor(18, 11);
		(lang == '1') ? printf("12.操作手册") : printf("12.Manual");
		cursor(12, 13);
		(lang == '1') ? printf("00.退出") : printf("00.Exit");
		(lang == '1') ? print_fixed("请选择交易信息！") : print_fixed("Select by number!");
		get_numeric_input(option_str, 2, 22, 15);
		option = atoi(option_str);
		switch(option)
		{
			case 0:
				quit();
				break;
			case 1:
				balance_inquiry();
				break;
			case 2:
				deposit();
				break;
			case 3:
				withdraw();
				break;
			case 4:
				transfer();
				break;
			case 5:
				forex();
				break;
			case 6:
				transaction_record();
				break;
			case 7:
				apply();
				break;
			case 8:
				schedule_inquiry();
				break;
			case 9:
				reset_password();
				break;
			case 10:
				freeze_account();
				break;
			case 11:
				ai_assistant();
				break;
			case 12:
				manual();
				break;
			default:
				clear(3, 14);
				(lang == '1') ? print_center(8, "无效输入") : print_center(8, "Invalid input");
				Sleep(1000);
				break;
		}
	}
}

//余额查询
void balance_inquiry()
{
	clear(3, 14);
	Balance balance;
	bool flag = false;
	FILE *fbalance = fopen("balance.txt", "r");
	if(fbalance == NULL)
	{
		(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
		Sleep(1000);
		return;
	}
	int read_status;
	while((read_status = read_balance(fbalance, &balance)) > 0)
	{
		if(strcmp(balance.account, current_account) == 0)
		{
			flag = true;
			break;
		}
	}
	fclose(fbalance);
	if(read_status < 0)
	{
		(lang == '1') ? print_center(8, "余额文件格式错误") : print_center(8, "Invalid balance file");
		Sleep(1000);
		return;
	}
	if(!flag)
	{
		strcpy(balance.account, current_account);
		balance.cny = 0;
		balance.usd = 0;
		balance.eur = 0;
		fbalance = fopen("balance.txt", "a");
		if (fbalance != NULL)
		{
			fprintf(fbalance, "%s %lld %lld %lld\n", balance.account, balance.cny, balance.usd, balance.eur);
			fclose(fbalance);
		}
	}
	(lang == '1') ? print_center(5, "您当前的余额为") : print_center(5, "Your current balance is");
	cursor(9, 8);
	printf("CNY: %lld.%02lld", balance.cny / 100, balance.cny % 100);
	cursor(9, 9);
	printf("USD: %lld.%02lld", balance.usd / 100, balance.usd % 100);
	cursor(9, 10);
	printf("EUR: %lld.%02lld", balance.eur / 100, balance.eur % 100);
	(lang == '1') ? print_center(12, "按任意键返回") : print_center(12, "Press any key to return");
	cursor(2, 15);
	getch();
}

//存款
void deposit()
{
	Balance balance;
	FILE *fbalance, *ftemp_balance, *fbill;
	char amount_str[8];
	long amount;
	char currency, *currency_str, exit_condition; //币种(1-人民币，2-美元，3-欧元)
	bool flag;
	while(1)
	{
		currency = '0';
		currency_str = "NO!";
		exit_condition = 'n';
		flag = false;
		clear(3, 14);
		(lang == '1') ? print_center(5, "请选择币种") : print_center(5, "Please select currency");
		print_center(8, "CNY(1)");
		print_center(10, "USD(2)");
		print_center(12, "EUR(3)");
		while(currency != '1' && currency != '2' && currency != '3')
		{
			cursor(2, 15);
			currency = getch();
			clear(15, 1);
		}
		clear(3, 14);
		(lang == '1') ? print_center(5, "请输入存款金额") : print_center(5, "Please enter the amount");
		(lang == '1') ? print_center(8, "输入0 返回菜单") : print_center(8, "Enter 0 to return");
		get_numeric_input(amount_str, 5, 2, 15);
		amount = atol(amount_str);
		clear(3, 14);
		if(amount == 0) return;
		if(amount < 0)
		{
			(lang == '1') ? print_center(8, "金额不能为负数") : print_center(8, "Amount cannot be negative");
			(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
			Sleep(1000);
			continue;
		}
		if(amount % 100 != 0)
		{
			(lang == '1') ? print_center(8, "请输入100的倍数") : print_center(8, "Please enter multiple of 100");
			(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
			Sleep(1000);
			continue;
		}
		if(amount > 20000)
		{
			(lang == '1') ? print_center(8, "单次存款最高 20000") : print_center(8, "Max 20000 per deposit");
			(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
			Sleep(1000);
			continue;
		}
		fbalance = fopen("balance.txt", "r");
		ftemp_balance = fopen("ftemp_balance.txt", "w");
		fbill = fopen("bill.txt", "a");
		if(fbalance == NULL || ftemp_balance == NULL || fbill == NULL)
		{
			if(fbalance) fclose(fbalance);
			if(ftemp_balance) fclose(ftemp_balance);
			if(fbill) fclose(fbill);
			remove("ftemp_balance.txt");
			(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
			Sleep(1000);
			return;
		}
		long long add_amount = (long long)amount * 100LL;
		int read_status;
		while((read_status = read_balance(fbalance, &balance)) > 0)
		{
			if(strcmp(current_account, balance.account) == 0)
			{
				flag = true;
				switch(currency)
				{
					case '1':
						if(balance.cny > LLONG_MAX - add_amount) goto balance_overflow;
						balance.cny += add_amount;
						currency_str = "CNY";
						break;
					case '2':
						if(balance.usd > LLONG_MAX - add_amount) goto balance_overflow;
						balance.usd += add_amount;
						currency_str = "USD";
						break;
					case '3':
						if(balance.eur > LLONG_MAX - add_amount) goto balance_overflow;
						balance.eur += add_amount;
						currency_str = "EUR";
						break;
				}
			}
			fprintf(ftemp_balance, "%s %lld %lld %lld\n", balance.account, balance.cny, balance.usd, balance.eur);
		}
		if(!flag)
		{
			flag = true;
			long long initial_cny = 0, initial_usd = 0, initial_eur = 0;
			switch(currency)
			{
				case '1':
					initial_cny = add_amount;
					currency_str = "CNY";
					break;
				case '2':
					initial_usd = add_amount;
					currency_str = "USD";
					break;
				case '3':
					initial_eur = add_amount;
					currency_str = "EUR";
					break;
			}
			fprintf(ftemp_balance, "%s %lld %lld %lld\n", current_account, initial_cny, initial_usd, initial_eur);
		}
		if(!replace_data_file(fbalance, ftemp_balance, "ftemp_balance.txt", "balance.txt", read_status == 0))
		{
			fclose(fbill);
			return;
		}
		fprintf(fbill, "%s %d %lld %s %lld %s\n", current_account, 0, add_amount, currency_str, (long long)time(NULL), "ATM存款");
		fclose(fbill);
		(lang == '1') ? print_center(5, "存款成功") : print_center(5, "Deposit successful");
		(lang == '1') ? cursor(7, 8) : cursor(5, 8);
		(lang == '1') ? printf("您已存入 %ld %s", amount, currency_str) : printf("Has deposited %ld %s", amount, currency_str);
		(lang == '1') ? print_center(10, "继续存款(1)") : print_center(10, "Continue deposit(1)");
		(lang == '1') ? print_center(12, "返回菜单(0)") : print_center(12, "Return to Menu(0)");
		while(exit_condition != '0' && exit_condition != '1')
		{
			cursor(2, 15);
			exit_condition = getch();
			clear(15, 1);
		}
		if(exit_condition == '0')
		{
			return;
		}
	}

balance_overflow:
	fclose(fbalance);
	fclose(ftemp_balance);
	fclose(fbill);
	remove("ftemp_balance.txt");
	clear(3, 14);
	(lang == '1') ? print_center(8, "超出余额上限") : print_center(8, "Balance limit exceeded");
	Sleep(1000);
}

//取款
void withdraw()
{
	Balance balance;
	FILE *fbalance, *ftemp_balance, *fbill;
	char amount_str[8];
	long amount;
	char currency, *currency_str, exit_condition;
	bool flag;
	while(1)
	{
		currency = '0';
		currency_str = "NO!";
		exit_condition = 'n';
		flag = false;
		clear(3, 14);
		(lang == '1') ? print_center(5, "请选择币种") : print_center(5, "Please select currency");
		print_center(8, "CNY(1)");
		print_center(10, "USD(2)");
		print_center(12, "EUR(3)");
		while(currency != '1' && currency != '2' && currency != '3')
		{
			cursor(2, 15);
			currency = getch();
			clear(15, 1);
		}
		clear(3, 14);
		(lang == '1') ? print_center(5, "请输入取款金额") : print_center(5, "Please enter the amount");
		(lang == '1') ? print_center(8, "输入0 返回菜单") : print_center(8, "Enter 0 to return");
		get_numeric_input(amount_str, 5, 2, 15);
		amount = atol(amount_str);
		clear(3, 14);
		if(amount == 0) return;
		if(amount < 0)
		{
			(lang == '1') ? print_center(8, "金额不能为负数") : print_center(8, "Amount cannot be negative");
			(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
			Sleep(1000);
			continue;
		}
		if(amount % 100 != 0)
		{
			(lang == '1') ? print_center(8, "请输入100的倍数") : print_center(8, "Please enter multiple of 100");
			(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
			Sleep(1000);
			continue;
		}
		if(amount > 20000)
		{
			(lang == '1') ? print_center(8, "单次取款最高 20000") : print_center(8, "Max 20000 per withdrawal");
			(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
			Sleep(1000);
			continue;
		}
		fbalance = fopen("balance.txt", "r");
		ftemp_balance = fopen("ftemp_balance.txt", "w");
		fbill = fopen("bill.txt", "a");
		if(fbalance == NULL || ftemp_balance == NULL || fbill == NULL)
		{
			if(fbalance) fclose(fbalance);
			if(ftemp_balance) fclose(ftemp_balance);
			if(fbill) fclose(fbill);
			remove("ftemp_balance.txt");
			(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
			Sleep(1000);
			return;
		}
		long long deduce_amount = (long long)amount * 100LL;
		int read_status;
		while((read_status = read_balance(fbalance, &balance)) > 0)
		{
			if(strcmp(current_account, balance.account) == 0)
			{
				switch(currency)
				{
					case '1':
						if(balance.cny >= deduce_amount)
						{
							flag = true;
							balance.cny -= deduce_amount;
							currency_str = "CNY";
						}
						break;
					case '2':
						if(balance.usd >= deduce_amount)
						{
							flag = true;
							balance.usd -= deduce_amount;
							currency_str = "USD";
						}
						break;
					case '3':
						if(balance.eur >= deduce_amount)
						{
							flag = true;
							balance.eur -= deduce_amount;
							currency_str = "EUR";
						}
						break;
				}
			}
			fprintf(ftemp_balance, "%s %lld %lld %lld\n", balance.account, balance.cny, balance.usd, balance.eur);
		}
		if(!flag && read_status == 0)
		{
			fclose(fbalance);
			fclose(ftemp_balance);
			fclose(fbill);
			remove("ftemp_balance.txt");
			(lang == '1') ? print_center(8, "余额不足") : print_center(8, "Insufficient balance");
			Sleep(1000);
			return;
		}
		if(!replace_data_file(fbalance, ftemp_balance, "ftemp_balance.txt", "balance.txt", read_status == 0))
		{
			fclose(fbill);
			return;
		}
		fprintf(fbill, "%s %d %lld %s %lld %s\n", current_account, 1, deduce_amount, currency_str, (long long)time(NULL), "ATM取款");
		fclose(fbill);
		(lang == '1') ? print_center(5, "取款成功") : print_center(5, "Withdrawal successful");
		(lang == '1') ? cursor(7, 8) : cursor(5, 8);
		(lang == '1') ? printf("您已取出 %ld %s", amount, currency_str) : printf("Has withdrawn %ld %s", amount, currency_str);
		(lang == '1') ? print_center(10, "继续取款(1)") : print_center(10, "Continue withdrawal(1)");
		(lang == '1') ? print_center(12, "返回菜单(0)") : print_center(12, "Return to Menu(0)");
		while(exit_condition != '0' && exit_condition != '1')
		{
			cursor(2, 15);
			exit_condition = getch();
			clear(15, 1);
		}
		if(exit_condition == '0')
		{
			return;
		}
	}
}

//转账
void transfer()
{
	char target_account[10], target_name[16];
	Balance balance;
	User user;
	FILE *fuser, *fbalance, *ftemp_balance, *fbill;
	char amount_str[12];
	char currency = '0', *currency_str = "NO!";
	bool target_found = false, target_frozen = false;
	bool my_balance_found = false, target_balance_found = false;
	//验证对方账号与姓名
	fuser = fopen("user.txt", "r");
	if(fuser == NULL)
	{
		clear(3, 14);
		(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
		Sleep(1000);
		return;
	}
	for(int i = 0; i < 3; i++)
	{
		target_found = false;
		target_frozen = false;
		clear(3, 14);
		(lang == '1') ? print_center(5, "请输入转账信息") : print_center(5, "Enter transfer info");
		(lang == '1') ? print_random(6, 8, "收款账号：") : print_random(4, 8, "Target Account:");
		(lang == '1') ? get_numeric_input(target_account, 9, 16, 8) : get_numeric_input(target_account, 9, 19, 8);
		(lang == '1') ? print_random(8, 10, "收款姓名：") : print_random(7, 10, "Target Name:");
		(lang == '1') ? get_name_input(target_name, 8, 18, 10) : get_name_input(target_name, 8, 19, 10);
		//防止转账给自己
		if(strcmp(target_account, current_account) == 0)
		{
			(lang == '1') ? print_fixed("不能给自己转账！") : print_fixed("Cannot transfer to yourself!");
			Sleep(1500);
			continue;
		}
		rewind(fuser);
		while(read_user(fuser, &user) > 0)
		{
			if(strlen(target_account) == 9 && strcmp(target_account, user.account) == 0 &&
			   strcmp(target_name, user.name) == 0)
			{
				if(user.status == 0)
				{
					target_frozen = true;
				}
				else
				{
					target_found = true;
				}
				break;
			}
		}
		if(target_found)
		{
			break; //验证通过
		}
		else
		{
			if(target_frozen)
			{
				(lang == '1') ? print_fixed("对方账户已被冻结！") : print_fixed("Target account is frozen!");
				Sleep(1500);
				fclose(fuser);
				return;
			}
			if(i < 2)
			{
				(lang == '1') ? print_fixed("账号或姓名错误，请重试！") : print_fixed("Account or name error!");
				Sleep(1500);
			}
		}
	}
	fclose(fuser);
	if(!target_found)
	{
		(lang == '1') ? print_fixed("验证失败，返回菜单") : print_fixed("Verification failed");
		Sleep(1000);
		return;
	}
	clear(3, 14);
	(lang == '1') ? print_center(5, "请选择币种") : print_center(5, "Please select currency");
	print_center(8, "CNY(1)");
	print_center(10, "USD(2)");
	print_center(12, "EUR(3)");
	while(currency != '1' && currency != '2' && currency != '3')
	{
		cursor(2, 15);
		currency = getch();
		clear(15, 1);
	}
	while(1)
	{
		clear(3, 14);
		(lang == '1') ? print_center(5, "请输入转账金额") : print_center(5, "Please enter the amount");
		(lang == '1') ? print_center(8, "输入0 返回菜单") : print_center(8, "Enter 0 to return");
		get_decimal_input(amount_str, 10, 2, 15);
		clear(3, 14);
		const char *dot = strchr(amount_str, '.');
		if(dot != NULL && strlen(dot + 1) > 2)
		{
			(lang == '1') ? print_center(8, "最多只能输入两位小数") : print_center(8, "Max 2 decimal places allowed");
			Sleep(1000);
			continue;
		}
		long long transfer_amount = atoll(amount_str) * 100;
		if(dot != NULL)
		{
			transfer_amount += (dot[1] - '0') * 10;
			if(dot[2] != '\0') transfer_amount += dot[2] - '0';
		}
		if(transfer_amount == 0) return;
		if(transfer_amount > 500000000LL)
		{
			(lang == '1') ? print_center(8, "单次转账最高 5000000") : print_center(8, "Max 5000000 per transfer");
			(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
			Sleep(1000);
			continue;
		}
		fbalance = fopen("balance.txt", "r");
		ftemp_balance = fopen("ftemp_balance.txt", "w");
		fbill = fopen("bill.txt", "a");
		if(fbalance == NULL || ftemp_balance == NULL || fbill == NULL)
		{
			if(fbalance) fclose(fbalance);
			if(ftemp_balance) fclose(ftemp_balance);
			if(fbill) fclose(fbill);
			remove("ftemp_balance.txt");
			(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
			Sleep(1000);
			return;
		}
		my_balance_found = false;
		target_balance_found = false;
		//第一遍读文件：校验自己余额是否足够
		while(read_balance(fbalance, &balance) > 0)
		{
			if(strcmp(balance.account, current_account) == 0)
			{
				my_balance_found = true;
				if((currency == '1' && balance.cny < transfer_amount) || (currency == '2' && balance.usd < transfer_amount) || (currency == '3' && balance.eur < transfer_amount))
				{
					fclose(fbalance);
					fclose(ftemp_balance);
					fclose(fbill);
					remove("ftemp_balance.txt");
					(lang == '1') ? print_center(8, "余额不足") : print_center(8, "Insufficient balance");
					Sleep(1000);
					return;
				}
				break;
			}
		}
		if(!my_balance_found)
		{
			fclose(fbalance);
			fclose(ftemp_balance);
			fclose(fbill);
			remove("ftemp_balance.txt");
			(lang == '1') ? print_center(8, "余额不足") : print_center(8, "Insufficient balance");
			Sleep(1000);
			return;
		}
		//第二遍重置光标：进行扣款与加款写操作
		rewind(fbalance);
		char my_remark[65], target_remark[65];
		if(currency == '1') currency_str = "CNY";
		else if(currency == '2') currency_str = "USD";
		else currency_str = "EUR";
		sprintf(my_remark, "转给%s(%s)", target_account, target_name);
		sprintf(target_remark, "%s转入", current_account);
		int read_status;
		while((read_status = read_balance(fbalance, &balance)) > 0)
		{
			if(strcmp(balance.account, current_account) == 0)
			{
				if(currency == '1') balance.cny -= transfer_amount;
				else if(currency == '2') balance.usd -= transfer_amount;
				else if(currency == '3') balance.eur -= transfer_amount;
			}
			else if(strcmp(balance.account, target_account) == 0)
			{
				target_balance_found = true;
				if(currency == '1')
				{
					if(balance.cny > LLONG_MAX - transfer_amount) goto balance_overflow;
					balance.cny += transfer_amount;
				}
				else if(currency == '2')
				{
					if(balance.usd > LLONG_MAX - transfer_amount) goto balance_overflow;
					balance.usd += transfer_amount;
				}
				else if(currency == '3')
				{
					if(balance.eur > LLONG_MAX - transfer_amount) goto balance_overflow;
					balance.eur += transfer_amount;
				}
			}
			fprintf(ftemp_balance, "%s %lld %lld %lld\n", balance.account, balance.cny, balance.usd, balance.eur);
		}
		if(!target_balance_found)
		{
			long long target_cny = 0, target_usd = 0, target_eur = 0;
			if(currency == '1') target_cny = transfer_amount;
			else if(currency == '2') target_usd = transfer_amount;
			else if(currency == '3') target_eur = transfer_amount;
			fprintf(ftemp_balance, "%s %lld %lld %lld\n", target_account, target_cny, target_usd, target_eur);
		}
		if(!replace_data_file(fbalance, ftemp_balance, "ftemp_balance.txt", "balance.txt", read_status == 0))
		{
			fclose(fbill);
			return;
		}
		fprintf(fbill, "%s %d %lld %s %lld %s\n", current_account, 3, transfer_amount, currency_str, (long long)time(NULL), my_remark);
		fprintf(fbill, "%s %d %lld %s %lld %s\n", target_account, 2, transfer_amount, currency_str, (long long)time(NULL), target_remark);
		fclose(fbill);
		//成功提示并退出函数
		(lang == '1') ? print_center(5, "转账成功") : print_center(5, "Transfer successful");
		(lang == '1') ? cursor(9, 8) : cursor(6, 8);
		(lang == '1') ? printf("成功转给 %s", target_name) : printf("Transferred to %s", target_name);
		cursor(10, 10);
		printf("%lld.%02lld %s", transfer_amount / 100, transfer_amount % 100, currency_str);
		(lang == '1') ? print_center(12, "按任意键返回") : print_center(12, "Press any key to return");
		cursor(2, 15);
		getch();
		return;
	}

balance_overflow:
	fclose(fbalance);
	fclose(ftemp_balance);
	fclose(fbill);
	remove("ftemp_balance.txt");
	clear(3, 14);
	(lang == '1') ? print_center(8, "超出余额上限") : print_center(8, "Balance limit exceeded");
	Sleep(1000);
}

//外汇(购汇)
void forex()
{
	Balance balance;
	FILE *fbalance, *ftemp_balance, *fbill;
	char buy_amount_str[8];
	long buy_amount;
	char target_curr = '0', pay_curr = '0';
	char *target_str = "NO!", *pay_str = "NO!";
	int target_rate = RATE_CNY, pay_rate = RATE_CNY;
	bool my_balance_found = false;
	//选择要购买的币种
	clear(3, 14);
	(lang == '1') ? print_center(5, "请选择购买币种") : print_center(5, "Select target currency");
	print_center(8, "CNY(1)");
	print_center(10, "USD(2)");
	print_center(12, "EUR(3)");
	while(target_curr != '1' && target_curr != '2' && target_curr != '3')
	{
		cursor(2, 15);
		target_curr = getch();
		clear(15, 1);
	}
	switch(target_curr)
	{
		case '1':
			target_str = "CNY";
			target_rate = RATE_CNY;
			break;
		case '2':
			target_str = "USD";
			target_rate = RATE_USD;
			break;
		case '3':
			target_str = "EUR";
			target_rate = RATE_EUR;
			break;
	}
	//输入购买金额
	while(1)
	{
		clear(3, 14);
		(lang == '1') ? print_center(5, "请输入购买金额") : print_center(5, "Please enter the amount");
		(lang == '1') ? print_center(8, "输入0 返回菜单") : print_center(8, "Enter 0 to return");
		get_numeric_input(buy_amount_str, 5, 2, 15);
		buy_amount = atol(buy_amount_str);
		clear(3, 14);
		if(buy_amount == 0) return;
		if(buy_amount < 0)
		{
			(lang == '1') ? print_center(8, "金额不能为负数") : print_center(8, "Amount cannot be negative");
			(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
			Sleep(1000);
			continue;
		}
		if(buy_amount > 20000)
		{
			(lang == '1') ? print_center(8, "单次购汇最高 20000") : print_center(8, "Max 20000 per forex");
			(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
			Sleep(1000);
			continue;
		}
		break;
	}
	long long target_cents = (long long)buy_amount * 100LL;
	//预存结算币种选项字符串，固定在第8行和第10行显示
	char pay1_str[10], pay2_str[10];
	char pay1_key, pay2_key;
	if(target_curr == '1')
	{
		strcpy(pay1_str, "USD(2)");
		pay1_key = '2';
		strcpy(pay2_str, "EUR(3)");
		pay2_key = '3';
	}
	else if(target_curr == '2')
	{
		strcpy(pay1_str, "CNY(1)");
		pay1_key = '1';
		strcpy(pay2_str, "EUR(3)");
		pay2_key = '3';
	}
	else
	{
		strcpy(pay1_str, "CNY(1)");
		pay1_key = '1';
		strcpy(pay2_str, "USD(2)");
		pay2_key = '2';
	}
	//选择结算币种
	while(1)
	{
		pay_curr = '0';
		clear(3, 14);
		(lang == '1') ? print_center(5, "请选择结算币种") : print_center(5, "Select payment currency");
		print_center(8, pay1_str);
		print_center(10, pay2_str);
		(lang == '1') ? print_center(12, "输入0 放弃购汇") : print_center(12, "Enter 0 to cancel");
		while(1)
		{
			cursor(2, 15);
			pay_curr = getch();
			if(pay_curr == '0')
			{
				clear(3, 14);
				(lang == '1') ? print_center(8, "购汇失败") : print_center(8, "Forex failed");
				Sleep(1000);
				return;
			}
			if(pay_curr == pay1_key || pay_curr == pay2_key)
			{
				break;
			}
			clear(15, 1);
		}
		clear(3, 14);
		switch(pay_curr)
		{
			case '1':
				pay_str = "CNY";
				pay_rate = RATE_CNY;
				break;
			case '2':
				pay_str = "USD";
				pay_rate = RATE_USD;
				break;
			case '3':
				pay_str = "EUR";
				pay_rate = RATE_EUR;
				break;
		}
		//计算需要支付的结算币种金额(单位：分，四舍五入)
		long long pay_cents = (target_cents * target_rate + pay_rate / 2) / pay_rate;
		//打开文件校验支付账户的余额
		fbalance = fopen("balance.txt", "r");
		if(fbalance == NULL)
		{
			(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
			Sleep(1000);
			return;
		}
		my_balance_found = false;
		bool balance_sufficient = false;
		while(read_balance(fbalance, &balance) > 0)
		{
			if(strcmp(balance.account, current_account) == 0)
			{
				my_balance_found = true;
				long long my_pay_bal = 0;
				if(pay_curr == '1') my_pay_bal = balance.cny;
				else if(pay_curr == '2') my_pay_bal = balance.usd;
				else if(pay_curr == '3') my_pay_bal = balance.eur;
				if(my_pay_bal >= pay_cents)
				{
					balance_sufficient = true;
				}
				break;
			}
		}
		fclose(fbalance);
		if(!my_balance_found || !balance_sufficient)
		{
			(lang == '1') ? print_center(8, "余额不足") : print_center(8, "Insufficient balance");
			(lang == '1') ? print_center(10, "请切换结算币种") : print_center(10, "Change payment currency");
			Sleep(1500);
			continue;
		}
		//余额充足，执行扣款与加款并写入两条账单
		fbalance = fopen("balance.txt", "r");
		ftemp_balance = fopen("ftemp_balance.txt", "w");
		fbill = fopen("bill.txt", "a");
		if(fbalance == NULL || ftemp_balance == NULL || fbill == NULL)
		{
			if(fbalance) fclose(fbalance);
			if(ftemp_balance) fclose(ftemp_balance);
			if(fbill) fclose(fbill);
			remove("ftemp_balance.txt");
			(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
			Sleep(1000);
			return;
		}
		char remark_pay[65], remark_buy[65];
		sprintf(remark_pay, "购汇消费%s", target_str);
		sprintf(remark_buy, "购入%s", target_str);
		int read_status;
		while((read_status = read_balance(fbalance, &balance)) > 0)
		{
			if(strcmp(balance.account, current_account) == 0)
			{
				// 扣除结算币种余额
				if(pay_curr == '1') balance.cny -= pay_cents;
				else if(pay_curr == '2') balance.usd -= pay_cents;
				else if(pay_curr == '3') balance.eur -= pay_cents;
				// 增加目标币种余额
				if(target_curr == '1')
				{
					if(balance.cny > LLONG_MAX - target_cents) goto balance_overflow;
					balance.cny += target_cents;
				}
				else if(target_curr == '2')
				{
					if(balance.usd > LLONG_MAX - target_cents) goto balance_overflow;
					balance.usd += target_cents;
				}
				else if(target_curr == '3')
				{
					if(balance.eur > LLONG_MAX - target_cents) goto balance_overflow;
					balance.eur += target_cents;
				}
			}
			fprintf(ftemp_balance, "%s %lld %lld %lld\n", balance.account, balance.cny, balance.usd, balance.eur);
		}
		if(!replace_data_file(fbalance, ftemp_balance, "ftemp_balance.txt", "balance.txt", read_status == 0))
		{
			fclose(fbill);
			return;
		}
		fprintf(fbill, "%s %d %lld %s %lld %s\n", current_account, 5, pay_cents, pay_str, (long long)time(NULL), remark_pay);
		fprintf(fbill, "%s %d %lld %s %lld %s\n", current_account, 4, target_cents, target_str, (long long)time(NULL), remark_buy);
		fclose(fbill);
		//成功提示并退出
		(lang == '1') ? print_center(5, "购汇成功") : print_center(5, "Forex successful");
		(lang == '1') ? cursor(8, 8) : cursor(7, 8);
		(lang == '1') ? printf("已购入: %ld %s", buy_amount, target_str) : printf("Purchase: %ld %s", buy_amount, target_str);
		cursor(7, 10);
		(lang == '1') ? printf("已扣除: %.2f %s", (double)pay_cents / 100.0, pay_str) : printf("Deduct: %.2f %s", (double)pay_cents / 100.0, pay_str);
		(lang == '1') ? print_center(12, "按任意键返回") : print_center(12, "Press any key to return");
		cursor(2, 15);
		getch();
		return;
	}

balance_overflow:
	fclose(fbalance);
	fclose(ftemp_balance);
	fclose(fbill);
	remove("ftemp_balance.txt");
	clear(3, 14);
	(lang == '1') ? print_center(8, "超出余额上限") : print_center(8, "Balance limit exceeded");
	Sleep(1000);
}

//交易记录
void transaction_record()
{
	clear(3, 14);
	//主区域提示
	(lang == '1') ? print_center(5, "您的交易记录如下") : print_center(5, "Your record is as follows");
	(lang == '1') ? print_center(8, "此区域不便显示") : print_center(8, "Cannot display in center");
	(lang == '1') ? print_center(10, "已在右侧为您显示") : print_center(10, "Show on the right side");
	(lang == '1') ? print_center(12, "按任意键返回") : print_center(12, "Press any key to return");
	FILE *fbill = fopen("bill.txt", "r");
	if(fbill == NULL)
	{
		cursor(2, 15);
		getch();
		return;
	}
	Bill temp_bill;
	long long timestamp;
	int print_lines = 0;
	char *type_str = "未知";
	char print_remark[65];
	//边遍历边顺序打印
	while(fscanf(fbill, "%9s %1d %18lld %3s %18lld %64s", temp_bill.account, &temp_bill.type, &temp_bill.amount, temp_bill.unit, &timestamp, temp_bill.remarks) == 6)
	{
		temp_bill.time = (time_t)timestamp;
		if(strcmp(temp_bill.account, current_account) == 0)
		{
			//类型映射(支持中英文)
			switch(temp_bill.type)
			{
				case 0:
					type_str = (lang == '1') ? "存款" : "Deposit";
					break;
				case 1:
					type_str = (lang == '1') ? "取款" : "Withdraw";
					break;
				case 2:
					type_str = (lang == '1') ? "转入" : "Inbound";
					break;
				case 3:
					type_str = (lang == '1') ? "转出" : "Outbound";
					break;
				case 4:
					type_str = (lang == '1') ? "购入" : "BuyForex";
					break;
				case 5:
					type_str = (lang == '1') ? "消费" : "ForexPay";
					break;
				default:
					type_str = (lang == '1') ? "其它" : "Other";
					break;
			}
			//备注翻译(英文语言下匹配固定的中文词汇进行替换，按 UTF-8 字节数定位)
			strcpy(print_remark, temp_bill.remarks);
			if(lang != '1')
			{
				if(strcmp(temp_bill.remarks, "ATM存款") == 0)
				{
					strcpy(print_remark, "ATM Deposit");
				}
				else if(strcmp(temp_bill.remarks, "ATM取款") == 0)
				{
					strcpy(print_remark, "ATM Withdraw");
				}
				else if(strstr(temp_bill.remarks, "转给") == temp_bill.remarks) //匹配以“转给”开头
				{
					snprintf(print_remark, sizeof(print_remark), "To %.61s", temp_bill.remarks + strlen("转给"));
				}
				else if(temp_bill.type == 2)
				{
					snprintf(print_remark, sizeof(print_remark), "From %.9s", temp_bill.remarks);
				}
				else if(strstr(temp_bill.remarks, "购汇消费") == temp_bill.remarks)
				{
					snprintf(print_remark, sizeof(print_remark), "Pay %.3s", temp_bill.remarks + strlen("购汇消费"));
				}
				else if(strstr(temp_bill.remarks, "购入") == temp_bill.remarks)
				{
					snprintf(print_remark, sizeof(print_remark), "Buy %.3s", temp_bill.remarks + strlen("购入"));
				}
			}
			cursor(35, 2 + print_lines);
			struct tm *t = localtime(&temp_bill.time);
			char time_str[24];
			if(t == NULL) continue;
			strftime(time_str, sizeof(time_str), "%Y/%m/%d %H:%M", t);
			//格式：时间  类型  金额  币种  备注
			printf("%s  %s  %.2f %s  %s", time_str, type_str, (double)temp_bill.amount / 100.0, temp_bill.unit, print_remark);
			print_lines++;
		}
	}
	fclose(fbill);
	cursor(2, 15);
	getch();
	//退出前清理右侧的记录
	for(int i = 0; i < print_lines; i++)
	{
		cursor(35, 2 + i);
		printf("                                                                 ");
	}
}

//大额申请
void apply()
{
	FILE *fapp;
	char currency, type_choice, *currency_str = "NO!";
	int app_type = 0; //0-存款，1-取款，2-转账，3-购汇
	char amount_str[12];
	long amount;
	int year, month, day;
	while(1)
	{
		currency = '0';
		type_choice = '0';
		//选择币种
		clear(3, 14);
		(lang == '1') ? print_center(5, "请选择币种") : print_center(5, "Please select currency");
		print_center(8, "CNY(1)");
		print_center(10, "USD(2)");
		print_center(12, "EUR(3)");
		while(currency != '1' && currency != '2' && currency != '3')
		{
			cursor(2, 15);
			currency = getch();
			clear(15, 1);
		}
		switch(currency)
		{
			case '1':
				currency_str = "CNY";
				break;
			case '2':
				currency_str = "USD";
				break;
			case '3':
				currency_str = "EUR";
				break;
		}
		//选择业务类型
		clear(3, 14);
		(lang == '1') ? print_center(5, "请选择业务类型") : print_center(5, "Select business type");
		(lang == '1') ? print_center(8, "存款(1)    取款(2)") : print_center(8, "Deposit(1) Withdraw(2)");
		(lang == '1') ? print_center(10, "转账(3)    购汇(4)") : print_center(10, "Transfer(3) Forex(4)");
		while(type_choice != '1' && type_choice != '2' && type_choice != '3' && type_choice != '4')
		{
			cursor(2, 15);
			type_choice = getch();
			clear(15, 1);
		}
		app_type = type_choice - '1'; //映射为0，1，2，3
		//输入申请金额
		clear(3, 14);
		(lang == '1') ? print_center(5, "请输入申请金额") : print_center(5, "Please enter the amount");
		(lang == '1') ? print_center(8, "输入0 返回菜单") : print_center(8, "Enter 0 to return");
		get_numeric_input(amount_str, 9, 2, 15);
		amount = atol(amount_str);
		clear(3, 14);
		if(amount == 0) return;
		//存/取/购汇限制：2万~200万
		if(app_type == 0 || app_type == 1 || app_type == 3)
		{
			if(amount <= 20000 || amount > 2000000)
			{
				(lang == '1') ? print_center(8, "金额需在2万到200万之间") : print_center(8, "Amount must be 20k-2M");
				(lang == '1') ? print_center(10, "请重新选择并输入") : print_center(10, "Please restart and retry");
				Sleep(1500);
				continue;
			}
		}
		//转账限制：500万~1亿
		else if(app_type == 2)
		{
			if(amount <= 5000000 || amount > 100000000)
			{
				(lang == '1') ? print_center(8, "金额需在500万到1亿之间") : print_center(8, "Amount must be 5M-100M");
				(lang == '1') ? print_center(10, "请重新选择并输入") : print_center(10, "Please restart and retry");
				Sleep(1500);
				continue;
			}
		}
		//输入预约日期
		while(1)
		{
			clear(3, 14);
			(lang == '1') ? print_center(5, "请输入预约日期") : print_center(5, "Enter reservation date");
			(lang == '1') ? print_center(8, "格式如：2022 09 19") : print_center(8, "Format: YYYY MM DD");
			(lang == '1') ? print_center(10, "输入0 0 0 取消预约") : print_center(10, "Enter 0 0 0 to cancel");
			cursor(2, 15);
			char date_input[32], extra;
			if(fgets(date_input, sizeof(date_input), stdin) == NULL) return;
			if(strchr(date_input, '\n') == NULL)
			{
				int ch;
				while((ch = getchar()) != '\n' && ch != EOF) {}
				if(ch == EOF) return;
				continue;
			}
			if(sscanf(date_input, "%4d%*1[ \t] %2d%*1[ \t] %2d %c", &year, &month, &day, &extra) != 3) continue;
			clear(3, 14);
			//取消预约
			if(year == 0 && month == 0 && day == 0)
			{
				(lang == '1') ? print_center(8, "取消预约") : print_center(8, "Cancelled");
				Sleep(1000);
				return;
			}
			//日期有效性检查
			if(!is_valid_date(year, month, day))
			{
				(lang == '1') ? print_center(8, "日期格式不合法") : print_center(8, "Invalid date format");
				(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
				Sleep(1500);
				continue;
			}
			//转换预约日期为时间戳(当天00:00:00)
			struct tm res_tm = {0};
			res_tm.tm_year = year - 1900;
			res_tm.tm_mon = month - 1;
			res_tm.tm_mday = day;
			res_tm.tm_isdst = -1;
			time_t reservation_time = mktime(&res_tm);
			//获取当前时间戳及当天00:00:00
			time_t now = time(NULL);
			struct tm *today_tm = localtime(&now);
			today_tm->tm_hour = 0;
			today_tm->tm_min = 0;
			today_tm->tm_sec = 0;
			today_tm->tm_isdst = -1;
			time_t today_start = mktime(today_tm);
			//不能早于今天
			if(reservation_time == (time_t)-1 || reservation_time < today_start)
			{
				(lang == '1') ? print_center(8, "预约日期不能早于今天") : print_center(8, "Date cannot be in past");
				(lang == '1') ? print_center(10, "请重试") : print_center(10, "Please retry");
				Sleep(1500);
				continue;
			}
			//数据校验完毕，写入文件
			fapp = fopen("appointment.txt", "a");
			if(fapp == NULL)
			{
				(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
				Sleep(1000);
				return;
			}
			long long app_amount = (long long)amount * 100LL; //转为“分”
			//写入格式：账号 金额 单位 类型 状态 预约时间戳 操作时间戳
			fprintf(fapp, "%s %lld %s %d %d %lld %lld\n", current_account, app_amount, currency_str, app_type, 0, (long long)reservation_time, (long long)now);
			fclose(fapp);
			//成功提示
			(lang == '1') ? print_center(5, "预约成功") : print_center(5, "Apply successful");
			(lang == '1') ? print_center(8, "请等待银行客服受理") : print_center(8, "Wait for approval");
			(lang == '1') ? print_center(12, "按任意键返回") : print_center(12, "Press any key to return");
			cursor(2, 15);
			getch();
			return;
		}
	}
}

//预约查询
void schedule_inquiry()
{
	clear(3, 14);
	//主区域提示
	(lang == '1') ? print_center(5, "您的预约记录如下") : print_center(5, "Your schedule as follows");
	(lang == '1') ? print_center(8, "此区域不便显示") : print_center(8, "Cannot display in center");
	(lang == '1') ? print_center(10, "已在右侧为您显示") : print_center(10, "Show on the right side");
	(lang == '1') ? print_center(12, "按任意键返回") : print_center(12, "Press any key to return");
	FILE *fapp = fopen("appointment.txt", "r");
	if(fapp == NULL)
	{
		cursor(2, 15);
		getch();
		return;
	}
	Appointment temp_app;
	long long datestamp, timestamp;
	int print_lines = 0;
	char *type_str = "未知";
	char *status_str = "未知";
	//遍历并顺序打印当前账户的预约记录
	while (fscanf(fapp, "%9s %18lld %3s %1d %1d %18lld %18lld", temp_app.account, &temp_app.amount, temp_app.unit, &temp_app.type, &temp_app.status, &datestamp, &timestamp) == 7)
	{
		temp_app.date = (time_t)datestamp;
		temp_app.time = (time_t)timestamp;
		if(strcmp(temp_app.account, current_account) == 0)
		{
			//业务类型映射
			switch(temp_app.type)
			{
				case 0:
					type_str = (lang == '1') ? "存款" : "Deposit";
					break;
				case 1:
					type_str = (lang == '1') ? "取款" : "Withdraw";
					break;
				case 2:
					type_str = (lang == '1') ? "转账" : "Transfer";
					break;
				case 3:
					type_str = (lang == '1') ? "购汇" : "Forex";
					break;
				default:
					type_str = (lang == '1') ? "其它" : "Other";
					break;
			}
			//受理状态映射
			switch(temp_app.status)
			{
				case 0:
					status_str = (lang == '1') ? "未受理" : "Pending";
					break;
				case 1:
					status_str = (lang == '1') ? "已受理" : "Approved";
					break;
				default:
					status_str = (lang == '1') ? "处理中" : "Processing";
					break;
			}
			cursor(35, 2 + print_lines);
			//预约日期(YYYY/MM/DD)
			struct tm *date_tm = localtime(&temp_app.date);
			char date_str[16];
			if(date_tm == NULL) continue;
			strftime(date_str, sizeof(date_str), "%Y/%m/%d", date_tm);
			//提交时间(YYYY/MM/DD HH:MM)
			struct tm *time_tm = localtime(&temp_app.time);
			char time_str[24];
			if(time_tm == NULL) continue;
			strftime(time_str, sizeof(time_str), "%Y/%m/%d %H:%M", time_tm);
			//格式：类型  金额  币种  预约日期  状态  [提交时间]
			printf("%s  %.2f  %s  %s  %s  [%s]", type_str, (double)temp_app.amount / 100.0, temp_app.unit, date_str, status_str, time_str);
			print_lines++;
		}
	}
	fclose(fapp);
	//等待用户按任意键返回
	cursor(2, 15);
	getch();
	//退出前清理右侧的记录
	for(int i = 0; i < print_lines; i++)
	{
		cursor(35, 2 + i);
		printf("                                                                   ");
	}
}

//修改密码
void reset_password()
{
	char old_pwd[7], new_pwd1[7], new_pwd2[7];
	User user;
	FILE *fuser, *ftemp_user;
	bool old_verified = false;
	//验证原密码(最多3次机会，输错3次直接退出系统)
	for(int i = 0; i < 3; i++)
	{
		clear(3, 14);
		(lang == '1') ? print_center(5, "请输入原密码") : print_center(5, "Enter current password");
		(lang == '1') ? print_center(8, "输入 exit!! 退出改密") : print_center(8, "Enter exit!! to cancel");
		(lang == '1') ? print_random(10, 12, "密码：") : print_random(8, 12, "Password: ");
		get_mask_password(old_pwd, 6);
		if(strcmp(old_pwd, "exit!!") == 0)
		{
			(lang == '1') ? print_fixed("已取消修改密码") : print_fixed("Password reset cancelled");
			Sleep(1000);
			return;
		}
		//读取文件对比原密码
		fuser = fopen("user.txt", "r");
		if(fuser == NULL)
		{
			clear(3, 14);
			(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
			Sleep(1000);
			return;
		}
		while(read_user(fuser, &user) > 0)
		{
			if(strcmp(user.account, current_account) == 0)
			{
				if(strlen(old_pwd) == 6 && is_all_digits(old_pwd) && strcmp(old_pwd, user.password) == 0)
				{
					old_verified = true;
				}
				break;
			}
		}
		fclose(fuser);
		if(old_verified)
		{
			break;
		}
		else
		{
			if(i < 2)
			{
				(lang == '1') ? print_fixed("原密码错误，请重试！") : print_fixed("Wrong password, retry!");
				Sleep(1500);
			}
			else
			{
				//三次密码错误触发安全限制，直接退出整个系统
				clear(3, 14);
				(lang == '1') ? print_center(8, "密码连续错误三次") : print_center(8, "Password failed 3 times");
				(lang == '1') ? print_fixed("触发安全限制，系统退出！") : print_fixed("Security trigger, exiting!");
				Sleep(2000);
				cursor(0, 20);
				exit(0);
			}
		}
	}
	//两次输入新密码(分行输入，支持3次匹配机会)
	for(int retry = 0; retry < 3; retry++)
	{
		clear(3, 14);
		(lang == '1') ? print_center(5, "请输入新密码") : print_center(5, "Enter new password");
		(lang == '1') ? print_random(8, 8, "新 密 码：") : print_random(6, 8, "New Password: ");
		(lang == '1') ? print_random(8, 10, "确认密码：") : print_random(6, 10, "Confirm Pwd: ");
		//输入第一次新密码
		(lang == '1') ? cursor(18, 8) : cursor(20, 8);
		get_mask_password(new_pwd1, 6);
		//判断是否触发退出指令
		if(strcmp(new_pwd1, "exit!!") == 0)
		{
			(lang == '1') ? print_fixed("已取消修改密码") : print_fixed("Password reset cancelled");
			Sleep(1000);
			return;
		}
		//密码规则校验：必须为6位纯数字
		if(strlen(new_pwd1) != 6 || !is_all_digits(new_pwd1))
		{
			(lang == '1') ? print_fixed("密码必须为6位纯数字！") : print_fixed("Password must be 6 digits!");
			Sleep(1500);
			continue;
		}
		//输入第二次新密码
		(lang == '1') ? cursor(18, 10) : cursor(19, 10);
		get_mask_password(new_pwd2, 6);
		//判断是否触发退出指令
		if(strcmp(new_pwd2, "exit!!") == 0)
		{
			(lang == '1') ? print_fixed("已取消修改密码") : print_fixed("Password reset cancelled");
			Sleep(1000);
			return;
		}
		//检查两次密码是否一致
		if(strcmp(new_pwd1, new_pwd2) != 0)
		{
			if(retry < 2)
			{
				(lang == '1') ? print_fixed("两次密码不匹配，请重试！") : print_fixed("Passwords do not match!");
				Sleep(1500);
			}
			else
			{
				(lang == '1') ? print_fixed("多次匹配失败，取消修改！") : print_fixed("Failed, reset cancelled!");
				Sleep(1500);
				return;
			}
		}
		else
		{
			//验证无误，更新user.txt
			fuser = fopen("user.txt", "r");
			ftemp_user = fopen("ftemp_user.txt", "w");
			if(fuser == NULL || ftemp_user == NULL)
			{
				if(fuser) fclose(fuser);
				if(ftemp_user) fclose(ftemp_user);
				remove("ftemp_user.txt");
				clear(3, 14);
				(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
				Sleep(1000);
				return;
			}
			int read_status;
			while((read_status = read_user(fuser, &user)) > 0)
			{
				if(strcmp(user.account, current_account) == 0)
				{
					strcpy(user.password, new_pwd1);
				}
				fprintf(ftemp_user, "%s %s %s %d\n", user.account, user.password, user.name, user.status);
			}
			if(!replace_data_file(fuser, ftemp_user, "ftemp_user.txt", "user.txt", read_status == 0)) return;
			FILE *foperation = fopen("operation.txt", "a");
			if(foperation != NULL)
			{
				fprintf(foperation, "%s %d %lld\n", current_account, 1, (long long)time(NULL));
				fclose(foperation);
			}
			//修改成功提示
			clear(3, 14);
			(lang == '1') ? print_center(5, "密码修改成功") : print_center(5, "Password reset success");
			(lang == '1') ? print_center(8, "请妥善保管您的新密码") : print_center(8, "Keep your new password safe");
			(lang == '1') ? print_center(12, "按任意键返回") : print_center(12, "Press any key to return");
			cursor(2, 15);
			getch();
			return;
		}
	}
}

//账户冻结
void freeze_account()
{
	char choice = 'n';
	User user;
	FILE *fuser, *ftemp_user;
	//询问界面
	clear(3, 14);
	(lang == '1') ? print_center(5, "是否确定冻结当前账户？") : print_center(5, "Freeze current account?");
	(lang == '1') ? print_center(8, "是(1)") : print_center(8, "Yes(1)");
	(lang == '1') ? print_center(10, "否(0)") : print_center(10, "No(0)");
	while(choice != '0' && choice != '1')
	{
		cursor(2, 15);
		choice = getch();
		clear(15, 1);
	}
	clear(3, 14);
	//选择“否”，取消冻结直接返回主菜单
	if(choice == '0')
	{
		(lang == '1') ? print_center(8, "已取消账户冻结") : print_center(8, "Freeze cancelled");
		Sleep(1000);
		return;
	}
	//选择“是”，写文件将status置为0
	fuser = fopen("user.txt", "r");
	ftemp_user = fopen("ftemp_user.txt", "w");
	if(fuser == NULL || ftemp_user == NULL)
	{
		if(fuser) fclose(fuser);
		if(ftemp_user) fclose(ftemp_user);
		remove("ftemp_user.txt");
		(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
		Sleep(1000);
		return;
	}
	int read_status;
	while((read_status = read_user(fuser, &user)) > 0)
	{
		//匹配当前登录账号，将其状态修改为0(冻结)
		if(strcmp(user.account, current_account) == 0)
		{
			user.status = 0;
		}
		fprintf(ftemp_user, "%s %s %s %d\n", user.account, user.password, user.name, user.status);
	}
	if(!replace_data_file(fuser, ftemp_user, "ftemp_user.txt", "user.txt", read_status == 0)) return;
	FILE *foperation = fopen("operation.txt", "a");
	if(foperation != NULL)
	{
		fprintf(foperation, "%s %d %lld\n", current_account, 2, (long long)time(NULL));
		fclose(foperation);
	}
	//提示并强制退出系统
	(lang == '1') ? print_center(5, "账户已被成功冻结") : print_center(5, "Account has been frozen");
	(lang == '1') ? print_center(8, "系统即将安全退出...") : print_center(8, "System exiting safely...");
	Sleep(1500);
	cursor(0, 20);
	exit(0);
}

//人工智能
void ai_assistant()
{
	char file_name[32];
	(lang == '1') ? strcpy(file_name, "faq_cn.txt") : strcpy(file_name, "faq_en.txt");
	FILE *fp = fopen(file_name, "r");
	if(fp == NULL)
	{
		clear(3, 14);
		(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
		Sleep(1000);
		return;
	}
	FAQItem faqs[50];
	char q_items[50][128];
	int faq_count = 0;
	char line[700];
	while(faq_count < 50 && fgets(line, sizeof(line), fp) != NULL)
	{
		line[strcspn(line, "\r\n")] = 0;
		if(strlen(line) == 0) continue;
		int id;
		char q[128] = {0}, a[512] = {0};
		if(sscanf(line, "%3d#%127[^#]#%511[^\n]", &id, q, a) == 3)
		{
			faqs[faq_count].id = id;
			strcpy(faqs[faq_count].question, q);
			strcpy(faqs[faq_count].answer, a);
			strcpy(q_items[faq_count], q); //提取问题列表
			faq_count++;
		}
	}
	fclose(fp);
	PagePos page_stack[100];
	int current_page = 1;
	page_stack[1].item_idx = 0;
	page_stack[1].char_offset = 0;
	while(1)
	{
		clear(3, 14);
		(lang == '1') ? print_center(4, "智能AI客服") : print_center(4, "AI Assistant");
		PagePos next_pos = render_items_page(q_items, faq_count, page_stack[current_page]);
		page_stack[current_page + 1] = next_pos;
		cursor(3, 14);
		(lang == '1') ? printf("<(1) >(2) 选择(3) 退出(0)") : printf("<(1) >(2) Select(3) exit(0)");
		cursor(2, 15);
		char cmd = getch();
		clear(15, 1);
		if(cmd == '0')
		{
			for(int r = 2; r <= 15; r++)
			{
				cursor(35, r);
				printf("                                         ");
			}
			return;
		}
		else if(cmd == '1' && current_page > 1)
		{
			current_page--;
			for(int r = 2; r <= 15; r++)
			{
				cursor(35, r);
				printf("                                         ");
			}
		}
		else if(cmd == '2')
		{
			if(next_pos.item_idx < faq_count)
			{
				current_page++;
				for(int r = 2; r <= 15; r++)
				{
					cursor(35, r);
					printf("                                         ");
				}
			}
			else
			{
				print_fixed((lang == '1') ? "已无更多数据！" : "No more data!");
				Sleep(500);
			}
		}
		else if(cmd == '3')
		{
			cursor(2, 15);
			int select_id = 0;
			if(lang == '1')
			{
				printf("输入问题标号：");
				char id_buf[8] = {0};
				get_numeric_input(id_buf, 3, 16, 15);
				select_id = atoi(id_buf);
			}
			else
			{
				printf("Enter No.: ");
				char id_buf[8] = {0};
				get_numeric_input(id_buf, 3, 13, 15);
				select_id = atoi(id_buf);
			}
			bool found = false;
			for(int i = 0; i < faq_count; i++)
			{
				if(faqs[i].id == select_id)
				{
					found = true;
					for(int r = 2; r <= 15; r++)
					{
						cursor(35, r);
						printf("                                         ");
					}
					cursor(35, 2);
					(lang == '1') ? printf("【AI 解答 %03d】", select_id) : printf("[AI Answer %03d]", select_id);
					int ans_len = strlen(faqs[i].answer);
					int a_idx = 0, a_row = 4;
					while(a_idx < ans_len && a_row <= 15)
					{
						cursor(35, a_row);
						int b_cnt = 0;
						int line_start = a_idx;
						while(a_idx < ans_len && b_cnt < 30)
						{
							int cb = get_char_bytes((const unsigned char *)&faqs[i].answer[a_idx]);
							int width = (cb > 1) ? 2 : 1;
							if(b_cnt + width > 30) break;
							b_cnt += width;
							a_idx += cb;
						}
						fwrite(faqs[i].answer + line_start, 1, a_idx - line_start, stdout);
						a_row++;
					}
					break;
				}
			}
			if(!found)
			{
				for(int r = 2; r <= 15; r++)
				{
					cursor(35, r);
					printf("                                         ");
				}
				cursor(35, 2);
				(lang == '1') ? printf("未找到相关问题解答") : printf("Question not found");
			}
			clear(15, 1);
		}
	}
}

//操作手册
void manual()
{
	char file_name[32];
	(lang == '1') ? strcpy(file_name, "manual_cn.txt") : strcpy(file_name, "manual_en.txt");
	FILE *fp = fopen(file_name, "r");
	if(fp == NULL)
	{
		clear(3, 14);
		(lang == '1') ? print_center(8, "系统错误") : print_center(8, "System error");
		Sleep(1000);
		return;
	}
	char items[50][128];
	int item_count = 0;
	while(item_count < 50 && fgets(items[item_count], sizeof(items[item_count]), fp) != NULL)
	{
		items[item_count][strcspn(items[item_count], "\r\n")] = 0;
		if(strlen(items[item_count]) > 0) item_count++;
	}
	fclose(fp);
	//页面历史栈：记录每一页开始时的(item_idx, char_offset)
	PagePos page_stack[100];
	int current_page = 1;
	page_stack[1].item_idx = 0;
	page_stack[1].char_offset = 0;
	while(1)
	{
		clear(3, 14);
		(lang == '1') ? print_center(4, "用户操作手册") : print_center(4, "User Manual");
		//渲染当前页，获取下一页的起点
		PagePos next_pos = render_items_page(items, item_count, page_stack[current_page]);
		page_stack[current_page + 1] = next_pos;
		//底部导航栏
		cursor(3, 14);
		(lang == '1') ? printf("<(1) >(2) 退出(0)") : printf("<(1) >(2) exit(0)");
		//如果已读到最后一条的末尾
		if(next_pos.item_idx >= item_count)
		{
			cursor(22, 14);
			(lang == '1') ? printf("(尾页)") : printf("(End)");
		}
		cursor(2, 15);
		char cmd = getch();
		clear(15, 1);
		if(cmd == '0') return;
		else if(cmd == '1' && current_page > 1)
		{
			current_page--;
		}
		else if(cmd == '2')
		{
			if(next_pos.item_idx < item_count)
			{
				current_page++;
			}
			else
			{
				print_fixed((lang == '1') ? "已无更多数据！" : "No more data!");
				Sleep(500);
			}
		}
	}
}

//退出
void quit()
{
	clear(3, 14);
	(lang == '1') ? print_center(5, "感谢使用KSCN银行 ATM") : print_center(5, "Thanks for using KSCN Bank");
	(lang == '1') ? print_center(8, "欢迎再次使用") : print_center(8, "Welcome to use again");
	for(int i = 3; i >= 0; i--)
	{
		(lang == '1') ? cursor(11, 10) : cursor(9, 10);
		(lang == '1') ? printf("%02d秒后退出", i) : printf("Exit in %02d sec", i);
		if(i > 0) Sleep(1000);
	}
	cursor(0, 20);
	exit(0);
}

// 获取完整 UTF-8 字符的字节数
int get_char_bytes(const unsigned char *str)
{
	int bytes = 1;
	if((str[0] & 0xE0) == 0xC0) bytes = 2;
	else if((str[0] & 0xF0) == 0xE0) bytes = 3;
	else if((str[0] & 0xF8) == 0xF0) bytes = 4;
	for(int i = 1; i < bytes; i++)
	{
		if((str[i] & 0xC0) != 0x80) return 1;
	}
	return bytes;
}

//密码输入函数：带*号遮罩、支持退格、满max_len位自动提交
void get_mask_password(char *out_pwd, int max_len)
{
	int count = 0;
	int ch;
	out_pwd[0] = '\0';
	while(1)
	{
		ch = getch();
		if(ch == 0 || ch == 0xE0)
		{
			getch();
			continue;
		}
		//回车键处理
		if(ch == '\r' || ch == '\n')
		{
			//必须已经输入了字符才允许回车提交
			if(count > 0)
			{
				out_pwd[count] = '\0';
				return;
			}
			continue;
		}
		//退格键Backspace处理
		if(ch == '\b')
		{
			if(count > 0)
			{
				count--;
				out_pwd[count] = '\0';
				printf("\b \b"); //原地擦除1位*号，光标左移
			}
			continue;
		}
		//正常字符输入(可限定只接收可打印字符，过滤无效特殊键)
		if(count < max_len && ch >= 32 && ch <= 126)
		{
			out_pwd[count++] = ch;
			out_pwd[count] = '\0';
			printf("*");
			//满指定位数(如6位)自动提交
			if(count == max_len)
			{
				return;
			}
		}
	}
}

//纯数字输入函数
void get_numeric_input(char *out_str, int max_len, int start_x, int start_y)
{
	int count = 0;
	int ch;
	out_str[0] = '\0';
	cursor(start_x, start_y);
	while(1)
	{
		ch = getch();
		if(ch == 0 || ch == 0xE0)
		{
			getch();
			continue;
		}
		//回车键处理
		if(ch == '\r' || ch == '\n')
		{
			//有内容时才允许回车提交
			if(count > 0)
			{
				out_str[count] = '\0';
				return;
			}
			//无内容时敲回车：重置光标到起点，不换行、不移动
			cursor(start_x + count, start_y);
			continue;
		}
		//退格键处理
		if(ch == '\b')
		{
			if(count > 0)
			{
				count--;
				out_str[count] = '\0';
				cursor(start_x + count, start_y);
				printf(" "); //用空格覆盖擦除
				cursor(start_x + count, start_y); //光标退回
			}
			continue;
		}
		//严格过滤：只有'0'~'9'数字才会被接受并在屏幕上回显
		if(count < max_len && ch >= '0' && ch <= '9')
		{
			out_str[count++] = ch;
			out_str[count] = '\0';
			putchar(ch); //在屏幕上打印数字，光标自然右移1格
		}
		//如果按下字母、符号等无效字符，直接不做任何操作，光标保持不动
	}
}

//支持小数点的数值输入函数(专用于转账等带小数金额)
void get_decimal_input(char *out_str, int max_len, int start_x, int start_y)
{
	int count = 0;
	bool has_dot = false; //标记是否已经输入过小数点
	int ch;
	out_str[0] = '\0';
	cursor(start_x, start_y);
	while(1)
	{
		ch = getch();
		if(ch == 0 || ch == 0xE0)
		{
			getch();
			continue;
		}
		//回车键处理：空输入时死卡在原地
		if(ch == '\r' || ch == '\n')
		{
			if(count > 0 && out_str[count - 1] != '.') //不能以单独的'.'结尾提交
			{
				out_str[count] = '\0';
				return;
			}
			cursor(start_x + count, start_y);
			continue;
		}
		//退格键处理
		if(ch == '\b')
		{
			if(count > 0)
			{
				count--;
				if(out_str[count] == '.') has_dot = false; //删除了小数点，解除标记
				out_str[count] = '\0';
				cursor(start_x + count, start_y);
				printf(" ");
				cursor(start_x + count, start_y);
			}
			continue;
		}
		//允许输入数字'0'~'9'，以及仅一次'.'
		if(count < max_len)
		{
			if(ch >= '0' && ch <= '9')
			{
				out_str[count++] = ch;
				out_str[count] = '\0';
				putchar(ch);
			}
			else if(ch == '.' && !has_dot && count > 0) //小数点不能作为首字符，且只能输入一次
			{
				has_dot = true;
				out_str[count++] = ch;
				out_str[count] = '\0';
				putchar(ch);
			}
		}
	}
}

// 姓名输入以控制台的 Unicode 按键为单位，按 UTF-8 保存。
void get_name_input(char *out_str, int max_width, int start_x, int start_y)
{
	HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
	INPUT_RECORD record;
	DWORD events;
	int count = 0, width = 0;
	out_str[0] = '\0';
	cursor(start_x, start_y);
	while(ReadConsoleInputW(input, &record, 1, &events) && events == 1)
	{
		if(record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;
		WCHAR ch = record.Event.KeyEvent.uChar.UnicodeChar;
		if(ch == '\r' && count > 0) return;
		if(ch == '\b' && count > 0)
		{
			do { count--; } while(count > 0 && ((unsigned char)out_str[count] & 0xC0) == 0x80);
			int erased_width = (unsigned char)out_str[count] < 0x80 ? 1 : 2;
			width -= erased_width;
			out_str[count] = '\0';
			cursor(start_x + width, start_y);
			printf("%*s", erased_width, "");
			cursor(start_x + width, start_y);
			continue;
		}
		if(ch <= L' ' || ch == 127 || ch == 0x3000) continue;
		char utf8[4];
		int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &ch, 1, utf8, sizeof(utf8), NULL, NULL);
		int char_width = bytes == 1 ? 1 : 2;
		if(bytes == 0 || width + char_width > max_width || count + bytes >= 16) continue;
		memcpy(out_str + count, utf8, bytes);
		count += bytes;
		out_str[count] = '\0';
		width += char_width;
		fwrite(utf8, 1, bytes, stdout);
	}
}

//校验字符串是否全为数字字符
bool is_all_digits(const char *str)
{
	if(strlen(str) == 0) return false;
	for(int i = 0; str[i] != '\0'; i++)
	{
		if(str[i] < '0' || str[i] > '9')
		{
			return false;
		}
	}
	return true;
}

//判断是否为闰年
bool is_leap_year(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

//校验年月日是否有效
bool is_valid_date(int year, int month, int day)
{
	if(year < 2022 || year > 2100) return false;
	if(month < 1 || month > 12) return false;
	if(day < 1) return false;
	int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if(month == 2 && is_leap_year(year))
	{
		days_in_month[2] = 29;
	}
	return day <= days_in_month[month];
}

//视图函数
//光标(横坐标，纵坐标)
void cursor(int x, int y)
{
	pos.X = x;
	pos.Y = y;
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), pos);
}

//清除(纵坐标，长度)
void clear(int y, int length)
{
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
	for(int i = y; i < y + length; i++)
	{
		pos.X = 2;
		pos.Y = i;
		SetConsoleCursorPosition(handle, pos);
		printf("                            ");
	}
}

//计算字符串在控制台上的物理显示宽度(中文占2字符宽，英文占1字符宽)
int get_display_width(const char *str)
{
	int width = 0;
	int i = 0;
	int len = strlen(str);
	while(i < len)
	{
		int bytes = get_char_bytes((const unsigned char *)&str[i]);
		width += (bytes > 1) ? 2 : 1;
		i += bytes;
	}
	return width;
}

//随机打印(横坐标，纵坐标，字符串)
void print_random(int x, int y, const char *str)
{
	cursor(x, y);
	printf("%s", str);
}

//居中打印(行号，字符串)
void print_center(int row, const char *str)
{
	int width = get_display_width(str);
	if(width > 28) width = 28;
	if(width % 2 != 0) width += 1; //向上取偶数
	int x = 16 - (width / 2);
	if(x < 2) x = 2; //不越过左边界
	print_random(x, row, str);
}

//固定打印(字符串)
void print_fixed(const char *str)
{
	print_random(2, 15, str);
}

//文本展示页面，返回下一页开始渲染的PagePos位置
PagePos render_items_page(char items[][128], int item_count, PagePos start_pos)
{
	int cur_row = 0; //当前占用行(0~6)
	int idx = start_pos.item_idx;
	int offset = start_pos.char_offset;
	while(idx < item_count && cur_row < 7)
	{
		//构造当前要打印的完整条目文本(从头开始带上001.标号，接上一页半句话直接用余下文本)
		char full_buf[160];
		if(offset == 0)
		{
			sprintf(full_buf, "%03d.%s", idx + 1, items[idx]);
		}
		else
		{
			strcpy(full_buf, items[idx] + offset);
		}
		int len = strlen(full_buf);
		int i = 0;
		//打印当前条目(可能跨多行)
		while(i < len && cur_row < 7)
		{
			cursor(3, 6 + cur_row);
			int cur_col = 0;
			int line_start = i;
			//凑满一行26个物理宽度
			while(i < len)
			{
				int bytes = get_char_bytes((const unsigned char *)&full_buf[i]);
				int width = (bytes > 1) ? 2 : 1;
				if(cur_col + width > 26) break; //本行满了
				cur_col += width;
				i += bytes;
			}
			//输出这一行的字符
			fwrite(full_buf + line_start, 1, i - line_start, stdout);
			cur_row++; //换下一行
		}
		//检查退出条件
		if(i < len)
		{
			//说明第7行填满了，但当前这条还没打印完！在半句话处截断
			PagePos next_pos;
			next_pos.item_idx = idx;
			//算上之前的offset偏移
			next_pos.char_offset = (offset == 0) ? (i - 4) : (offset + i);
			return next_pos;
		}
		//当前条目完整打印完毕，准备打印下一条
		idx++;
		offset = 0; // 下一条从头开始
	}
	//文本全部打印完毕
	PagePos end_pos = {idx, 0};
	return end_pos;
}

//系统页面
void page()
{
	printf("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
	printf("$                              $\n");
	printf("$ ￥￥￥￥￥￥￥￥￥￥￥￥￥￥ $\n");
	printf("$                              $\n");
	printf("$                              $\n");
	printf("$       欢迎来到KSCN银行       $\n");
	printf("$                              $\n");
	printf("$                              $\n");
	printf("$          请选择语言          $\n");
	printf("$                              $\n");
	printf("$          中文     1          $\n");
	printf("$                              $\n");
	printf("$          English  2          $\n");
	printf("$                              $\n");
	printf("$                              $\n");
	printf("$                              $\n");
	printf("$                              $\n");
	printf("$ ￥￥￥￥￥￥￥￥￥￥￥￥￥￥ $\n");
	printf("$                              $\n");
	printf("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n");
}
