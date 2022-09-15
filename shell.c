#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_ARGS 8 //单条命令的参数个数上限
#define MAX_COMMANDS 8 //管道连接的命令个数上限
#define BUF_SIZE 128 //命令行字符个数上限
#define MAX_HISTORY 100 //历史命令记录上限

#define ERROR_EMPTY(errorStr) do {fprintf(stderr,"%sPlease check and try again :D\n",errorStr); return false;} while(0)
#define ERROR_IOFILE do {fprintf(stderr,"\033[1;31mError:\033[0m I/O file format error! \nPlease check and try again :D\n"); return false;} while(0)
#define ERROR_STR do {fprintf(stderr,"\033[1;31mError:\033[0m Your str miss \' or \" ! \nPlease check and try again :D\n"); return false;} while(0)
#define ERROR_ENV do {fprintf(stderr,"\033[1;31mError:\033[0m Your variable miss ! \nPlease check and try again :D\n"); return false;} while(0)
#define ERROR_FORK do {fprintf(stderr,"\033[1;31mError:\033[0m Failed to fork process!"); return; } while(0)
#define ERROR_OPEN do {fprintf(stderr,"\033[1;31mError:\033[0m Failed to open file! \nPlease check and try again :D\n");exit(1); return;} while(0)
#define ERROR_EXECUTE(errorCmd) do {fprintf(stderr, "\033[1;31mError:\033[0m Failed to execute cmd %s!\n", errorCmd);exit(1); return; } while(0)
#define ERROR_EXIT do {fprintf(stderr, "\033[1;31mError:\033[0m Failed to exit with status %d!\n", WEXITSTATUS(status)); return; } while(0)
#define ERROR_FMT(errorCmd) do {fprintf(stderr, "\033[1;31mError:\033[0m Please read help doc and check the usage of %s!\n", errorCmd); return; } while(0)

FILE *fp;
extern char **environ;
char buf[BUF_SIZE];
char *start;
char **args;
int commandsCount = 0; //记录指令的个数
int status; //子进程退出状态
int cmdnum = 0; //记录历史命令的个数
char history[MAX_HISTORY][BUF_SIZE]; //存放历史命令
char pathbak[BUF_SIZE]; //存放工作路径

//指令结构体
typedef struct {
    char *read; //输入重定向
    char *write; //追加输出重定向
    char *overwrite; //覆盖输出重定向
    char **args; //参数
    char *cmd; //命令名
    int cnt;
} Command;

void createCommand(Command *cmd) {
    cmd->read = cmd->overwrite = cmd->write = NULL;
    cmd->args = NULL;
    cmd->cmd = NULL;
    cmd->cnt = 0;
}

void freeCommand(Command *command) {
    free(command->read);
    free(command->write);
    free(command->overwrite);
    free(command->cmd);
    for (char **args = command->args; *args; ++args) free(*args);
    free(command->args);
}

//history
void saveHistory(char *str) {
    if (cmdnum >= MAX_HISTORY) {
        cmdnum = 0;
        memset(history, 0, sizeof history);
    }
    strcpy(history[cmdnum++], str);
}

void printHistory(Command *command) {
    //history
     if (command->cnt == 1 || command->cnt == 3) {
        for (int i = 0; i < cmdnum; ++i) {
            printf("%d  %s", i + 1, history[i]);
        }
        return ;
    }
    //history + num
    else if (command->cnt == 2 || command->cnt == 4) {
        //check the argument
        for (char *c = command->args[1]; *c; ++c) {
            if (*c < '0' || *c > '9') 
                ERROR_FMT(command->cmd);
        }
        int n = atoi(command->args[1]);
        if (n > cmdnum) n = cmdnum;
        for (int i = n; i > 0 && cmdnum - i >= 0; --i) {
            printf("%d  %s", cmdnum - i + 1, history[cmdnum - i]);
        }
        return;
    }
}

void callHistory(Command *command, char history[][BUF_SIZE]) {
    //history 或 history + num
    if (command->cnt == 1 || command->cnt == 2) {
        printHistory(command);
        return ;
    }
    //参数不符合要求
    else if (command->cnt > 4 || command->read || (!command->write && !command->overwrite)) { ERROR_FMT(command->cmd); }
    //重定向操作
    else if (command->cnt == 3 || command->cnt == 4) {
        int fd;
        int out = dup(1); //暂存文件描述符
        char *file = command->write ? command->write : command->overwrite;
        if (command->write) {
            fd = open(file, O_WRONLY | O_TRUNC | O_CREAT, 0666); 
        }
        else if (command->overwrite) {
            fd = open(file, O_WRONLY | O_APPEND | O_CREAT, 0666); 
        }
        if (fd < 0) {fprintf(stderr, "\033[1;31mError:\033[0m Failed to open file %s\n", file); return; }
        dup2(fd, 1);
        printHistory(command);
        dup2(out, 1); //恢复文件描述符
        close(fd);
        close(out);
        return ;
    }
}

//cd
void callCd(Command *command) {
    char *path = NULL;
    //cd
    if (command->cnt == 1) {
       if ((path = getenv("HOME")) == NULL) {perror("getenv()"); return;}
    }
    //cd + pathname
    else if (command->cnt == 2) {
        if (strcmp(command->args[1], "-") == 0) {
            if (strlen(pathbak) == 0) {    
                if ((path = getcwd(NULL, 0)) == NULL) {perror("getcwd()"); return;}
            }
            else path = strndup(pathbak, BUF_SIZE);
        }
        else {
            path = strndup(command->args[1], BUF_SIZE);
        }
    }
    else if (command->cnt >= 3) {
        fprintf(stderr, "\033[1;31mError:\033[0m Too many args!\n");
        return;
    }
    //保存当前工作路径
    if (getcwd(pathbak, BUF_SIZE) == NULL) {perror("getcwd()"); return;}
    //chdir true 0, false -1
    if (chdir(path)) {
        fprintf(stderr, "\033[1;31mError:\033[0m Failed to cd :%s\n", path);
    }
    free(path);
    return;
}

//set
void printEnv() {
    for (int i = 0; environ[i] != 0; ++i) {
        puts(environ[i]);
    }
    return ;
}
void callSet(Command *command) {
    //无参数直接输出当前用户环境变量
    if (command->cnt == 1) {
        printEnv();
        return;
    }
    //参数不符合条件
    else if (command->cnt >= 4 || command->cnt == 2 || command->read) {ERROR_FMT(command->cmd); }
    //有参数时直接重定向至文件或者设置环境变量的值
    else if (command->cnt == 3) {
        if (!command->write && !command->overwrite) {
            char *env = NULL;
            if ((env = getenv(command->args[1])) == NULL) {perror("getenv()"); return;}
            setenv(command->args[1], command->args[2], 1);
            return ;
        }
        int fd;
        int out = dup(1); //暂存文件描述符
        char *file = command->write ? command->write : command->overwrite;
        if (command->write) {
            fd = open(file, O_WRONLY | O_TRUNC | O_CREAT, 0666); 
        }
        else if (command->overwrite) {
            fd = open(file, O_WRONLY | O_APPEND | O_CREAT, 0666); 
        }
        if (fd < 0) {fprintf(stderr, "\033[1;31mError:\033[0mFailed to open file %s\n", file); return; }
        dup2(fd, 1);
        printEnv();
        dup2(out, 1); //恢复文件描述符
        close(fd);
        close(out);
        return ;
    }
}

//unset
void callUnset(Command *command) {
    //参数不符合条件
    if (command->cnt != 2) {ERROR_FMT(command->cmd); }
    //删除相应环境变量
    else {
        char *env = NULL;
        if ((env = getenv(command->args[1])) == NULL) {perror("getenv()"); return;}
        setenv(command->args[1], "", 1); 
        return ;
    }
}

//fg
//bg

//umask
void callUmask(Command *command) {
    //无参数时直接显示当前umask值
    if (command->cnt == 1) {
        mode_t maskbak = umask(0);
        umask(maskbak);
        printf("%04o\n", maskbak);
        return;
    }
    else if (command->cnt > 2 || strlen(command->args[1]) > 4) {
        ERROR_FMT(command->cmd);
    }
    //参数个数与长度要求
    //判断参数值是否合法
    else {
        for (int i = 0; i < strlen(command->args[1]); ++i) {
            if (command->args[1][i] >= '0' && command->args[1][i] <= '7')  continue;
            else ERROR_FMT(command->cmd);
        }
        unsigned int mask = 0;
        for (int i = strlen(command->args[1]) - 1, j = 0; i >= 0; --i, ++j) {
            mask += pow(8, j ) * (command->args[1][i] - '0');
        }

        umask(mask);
    }
    return ;
}

//test
void callTest(Command *command) {
    
}
////jobs
//void callJobs() {
    
//}

//help
void printHelp() {
  printf("欢迎查看zsh的用户手册！\n");
  printf("\n");
  printf("*******************************************************************************\n");
  printf("Chapter 1:  Shell-Builtin\n\n");
  printf("1. bg\n");
  printf("命令作用：  将最近一个挂起的进程转为后台执行\n");
  printf("使用示例：  bg\n");
  printf("参数个数：  无参数\n");
  printf("\n");
  
  printf("2. cd\n");
  printf("命令作用：  无参数则默认为家目录，有参数则改变当前目录为参数内容，-号代表回到之前的目录\n");
  printf("使用示例：  cd\n");
  printf("使用示例：  cd /home\n");
  printf("使用示例：  cd -\n");
  printf("参数个数：  无参数或1个参数\n");
  printf("\n");  

  printf("3. exec\n");
  printf("命令作用：  使用参数指定的命令替换当前进程\n");
  printf("使用示例：  exec ls\n");
  printf("参数个数：  1个参数\n");
  printf("\n"); 

  printf("4. exit\n");
  printf("命令作用：  退出当前进程\n");
  printf("使用示例：  exit\n");
  printf("参数个数：  无参数\n");
  printf("\n"); 
  
  printf("5. fg\n");
  printf("命令作用：  将最近的一个后台任务转到前台执行\n");
  printf("使用示例：  fg\n");
  printf("参数个数：  无参数\n");
  printf("\n"); 
  
  printf("6. help\n");
  printf("命令作用：  显示用户手册\n");
  printf("使用示例：  help\n");
  printf("使用示例：  help > test.txt\n");
  printf("使用示例：  help | cmd\n");
  printf("\n"); 
  
  printf("7. history\n");
  printf("命令作用：  显示历史命令，默认最多存储100条\n");
  printf("使用示例：  history\n");
  printf("使用示例：  history 10\n");
  printf("使用示例：  history 10 > test.txt\n");
  printf("使用示例：  history 10 | cmd\n");
  printf("\n"); 

  printf("8. jobs\n");
  printf("命令作用：  显示所有的后台进程\n");
  printf("使用示例：  jobs\n");
  printf("参数个数：  无参数\n");
  printf("\n"); 
  
  printf("9. set\n");
  printf("命令作用：  无参数时，显示所有环境变量；有2个参数时，设置第1个参数代表的环境变量的值为第2个参数\n");
  printf("使用示例：  set\n");
  printf("使用示例：  set USER ZS\n");
  printf("参数个数：  无参数或2个参数\n");
  printf("\n");
  
  printf("10. test\n");
  printf("命令作用：  判断两字符串是否相等以及两数字之间的大小关系(相等，不相等，大于，小于，大于等于，小于等于)\n");
  printf("使用示例：  test abc = abc\n");
  printf("使用示例：  test abc != abc\n");
  printf("使用示例：  test 2 -eq 2\n");
  printf("使用示例：  test 2 -ne 2\n");
  printf("使用示例：  test 2 -gt 2\n");
  printf("使用示例：  test 2 -ge 2\n");
  printf("使用示例：  test 2 -lt 2\n");
  printf("使用示例：  test 2 -le 2\n");
  printf("参数个数：  3个参数\n");
  printf("\n");
  
  printf("11. umask\n");
  printf("命令作用：  无参数时，显示当前掩码；有1个参数时，将当前掩码修改为参数值\n");
  printf("使用示例：  umask\n");
  printf("使用示例：  umask 0222\n");
  printf("参数个数：  无参数或1个参数\n");
  printf("\n");

  printf("12. unset\n");
  printf("命令作用：  删除环境变量\n");
  printf("使用示例：  unset USER\n");
  printf("参数个数：  1个参数\n");
  printf("\n");  
  printf("*******************************************************************************\n");
  printf("\n\n");
  
  printf("*******************************************************************************\n");
  printf("Chapter 2: 外部命令\n\n");
  printf("简单描述： 除了内建命令之外，zsh还能够自动查找并执行外部命令\n\n");
  printf("实现原理： 其他的命令行输入被解释为程序调用，zsh通过fork()创建子进程，然后在子进程中调用execvp()函数来查找实现进程替换，如果没有找到则会输出相应的错误提示信息\n\n");
  printf("使用示例： ls -l\n");
  printf("使用示例： gedit test.txt\n");
  printf("*******************************************************************************\n");
  printf("\n\n");
  
  printf("*******************************************************************************\n");
  printf("Chapter 3: 脚本文件的执行\n\n");
  printf("简单描述： zsh能够执行脚本文件中的命令，在调用zsh时，如果不加参数则进入命令行输入模式，如果加上一个脚本文件的参数，则会从参数代表的文件中提取命令并执行\n\n");
  printf("实现原理： 在检查到命令行参数时，zsh将打开参数代表的文件，之后用readCommand()函数读取命令时，从文件流中读取内容到buf，而不是从标准输入流中读取，如果打开文件失败则输出相应的错误提示信息\n\n");
  printf("使用示例： zsh test.sh\n");
  printf("*******************************************************************************\n");
  printf("\n\n");
  
  printf("*******************************************************************************\n");
  printf("Chapter 4: I/O重定向\n\n");
  printf("简单描述： zsh能够支持I/O重定向，在输入要执行的命令后，输入‘<’，再接输入重定向到的文件inputfile，zsh在执行命令时就会从inputfile中读取而非从标准输入中读取；输入‘>’或者‘>>’再接输出重定向到的文件outputfile，zsh就会将命令执行的结果输出到outputfile中而非输出到屏幕上，其中‘>’表示覆盖写，‘>>’表示追加写\n\n");
  printf("实现原理： 在命令行解析的过程中检查到‘<’，‘>’或者‘>>’时，尝试获取后面的文件名，将其保存在指令结构体中。之后执行命令时用open()函数打开文件(只读、覆盖写、追加写)，再用dup2()函数用打开的文件流替换相应的标准输出或输入流，即完成了重定向操作\n\n");
  printf("使用示例： wc < test1.txt >> test2.txt\n");
  printf("*******************************************************************************\n");
  printf("\n\n");
  
  printf("*******************************************************************************\n");
  printf("Chapter 5: 后台程序执行\n\n");
  printf("简单描述： zsh支持后台程序执行，在命令后输入字符&\n\n");
  printf("实现原理： 在命令解析的过程中检查到&时，将预先定义的表示是否后台执行的标志置1。利用fork()函数创建子进程来执行命令，但在主进程中，使用waitpid()函数的WNOHANG选项，不阻塞主进程，这样就实现了命令在后台执行，主进程仍然可以进行其他操作\n\n");
  printf("使用示例： sleep 10 &\n");
  printf("*******************************************************************************\n");
  printf("\n\n");
  
  printf("*******************************************************************************\n");
  printf("Chapter 6: 管道\n\n");
  printf("简单描述： zsh支持多级管道操作\n\n");
  printf("实现原理： 在命令解析的过程中检查到|时，指令计数器自增，在指令个数大于1的情况下默认使用了管道。执行指令时首先调用pipe()函数创建无名管道，管道两端的文件描述符分别保存在pipe[0]和pipe[1]中。一个进程将信息写到管道内，另一个进程再从管道内读取信息，就完成了两个进程之间的通信。主进程利用fork()函数先创建一个子进程pid1，在pid1中，将标准输出重定向到管道的写端，并执行命令，命令的输出将写进管道中。主进程首先用waitpid()函数阻塞等待，等待子进程pid1返回，待pid1返回后，再次利用fork()函数创建一个子进程pid2，在pid2中，将标准输入重定向到管道的读端，命令的输入将从管道中读取。在主进程中，用waitpid()函数阻塞主进程，等待子进程pid2返回，待pid2返回后，利用close()函数关闭管道两端即可。多级管道同理\n\n");
  printf("使用示例： ls | wc\n");
  printf("*******************************************************************************\n");
  printf("\n\n");
  
  printf("*******************************************************************************\n");
  printf("Chapter 7: 环境变量\n\n");
  printf("简单描述： zsh利用env显示所有的环境变量，修改环境变量则使用getenv()，setenv()等函数即可实现set和unset。此外还利用shmget()函数创建了一段共享内存用于存储后台进程表的相关信息，可以被所有进程访问\n\n");
  printf("*******************************************************************************\n");
  printf("\n");
  
  printf("*******************************************************************************\n");
  printf("Chapter 8: 信号处理\n\n");
  printf("简单描述： ctl + c会终止前台进程运行，若无进程则持续打印命令提示符。ctrl + z会挂起前台进程，若无进程则无影响。后台进程不受终端组合键的影响。默认屏蔽其他信号\n\n");
  printf("*******************************************************************************\n");
  printf("\n");
}

void callHelp(Command *command) {
    //无参数时输出重定向到less分页程序显示
    //创建两个子进程执行即可
    if (command->cnt == 1) {
        int fd[2];
        if (pipe(fd) < 0) {perror("pipe()"); return; }
        pid_t pid1 = fork();
        if (pid1 < 0) ERROR_FORK;
        else if (pid1 == 0) {
            close(fd[0]);
            dup2(fd[1], 1);
            printHelp();
            close(fd[1]);
            exit(0);
        }
        else {
            wait(&status);
            if (!WIFEXITED(status)) ERROR_EXIT;
            pid_t pid2 = fork();
            if (pid2 < 0) ERROR_FORK;
            else if (pid2 == 0) {
                close(fd[1]);
                dup2(fd[0], 0);
                //execl("/usr/bin/less", "less", NULL);
                execl("/bin/more", "more", NULL);
                close(fd[0]);
                exit(0);
            }
            else {
                //先关闭文件描述符防止阻塞
                close(fd[0]);
                close(fd[1]);
                wait(&status);
                if (!WIFEXITED(status)) ERROR_EXIT;
                return ;
            }
        }
    }
    //参数不符合条件
    else if (command->cnt > 4 || command->cnt == 2 || command->read || (!command->write && !command->overwrite)) {ERROR_FMT(command->cmd); }
    //有参数时直接重定向至文件
    else if (command->cnt == 3) {
        int fd;
        int out = dup(1); //暂存文件描述符
        char *file = command->write ? command->write : command->overwrite;
        if (command->write) {
            fd = open(file, O_WRONLY | O_TRUNC | O_CREAT, 0666); 
        }
        else if (command->overwrite) {
            fd = open(file, O_WRONLY | O_APPEND | O_CREAT, 0666); 
        }
        if (fd < 0) {fprintf(stderr, "\033[1;31mError:\033[0mFailed to open file %s\n", file); return; }
        dup2(fd, 1);
        printHelp();
        dup2(out, 1); //恢复文件描述符
        close(fd);
        close(out);
        return ;
    }
}

bool fetchFileName(char **bufAddr, char **cmdFileAddr) {
    start = (*bufAddr);
    char *buf = *bufAddr;
    while ((*buf != '\n') && (!isspace(*buf)) && (*buf != '|') && (*buf != '<') && (*buf != '>')) buf = ++(*bufAddr);
    if (buf == start) return false;

    (*cmdFileAddr) = malloc(sizeof(char) * (buf - start + 1));
    memcpy((*cmdFileAddr), start, sizeof(char) * (buf - start));
    (*cmdFileAddr)[buf - start] = '\0';
    return true;
}

bool splitCommands(char *buf, Command *commands) {
    int waitCommand = 1;
    createCommand(commands);
    args = commands->args = malloc(sizeof(char *) * MAX_ARGS);
    
    while (1) {
        switch(*buf) {
            case ' ' : 
                while ((*buf != '\n') && isspace(*buf)) {++buf;} //遇到非换行的空白字符就继续后移
                break;

            case '\n': 
                //仅支持单行命令，因此遇到换行符就表明命令结束，如果此时仍然处于等待命令状态则报错，除非没有输入过命令
                if (commandsCount == 1 && commands->cnt == 0) return false;
                if (waitCommand) ERROR_EMPTY("\033[1;31mError:\033[0m Next command shouldn't be empty!\n");
                *args = malloc(sizeof(char));
                *args = 0; //NULL结尾
                commands++;
                commands->cmd = NULL;
                return true;
                
            case '|' : 
                if (waitCommand) ERROR_EMPTY("\033[1;31mError:\033[0m Pipe should be used after a command!\n");
                waitCommand = 1;
                buf++;
                *args = malloc(sizeof(char));
                *args = 0; //NULL结尾
                commandsCount++;
                commands++;
                createCommand(commands);
                args = commands->args = malloc(sizeof(char *) * MAX_ARGS);
                break; 

            case '<' : 
                if (waitCommand) ERROR_EMPTY("\033[1;31mError:\033[0m I/O redirection should be used after a command!\n");
                buf++;
                while ((*buf != '\n') && isspace(*buf)) ++buf;
                if (fetchFileName(&buf, &(commands->read)) == false) ERROR_IOFILE;
                commands->cnt += 2;
                break;

            case '>' : 
                if (waitCommand) ERROR_EMPTY("\033[1;31mError:\033[0m I/O redirection should be used after a command!\n");
                buf++;
                while ((*buf != '\n') && isspace(*buf)) ++buf;
                if (*(buf) != '>') { if(fetchFileName(&buf, &(commands->write)) == false) ERROR_IOFILE; commands->cnt += 2; break; }
                buf++;
		        while ((*buf != '\n') && isspace(*buf)) {++buf;}
                if (fetchFileName(&buf, &(commands->overwrite)) == false) ERROR_IOFILE; commands->cnt += 2; break;
            
            case '$' : 
                if (waitCommand) ERROR_EMPTY("\033[1;31mError:\033[0m $ should be used after a command!\n");
                start = ++buf;
                while ((*buf != '\n') && (!isspace(*buf)) && (*buf != '|') && (*buf != '<') && (*buf != '>')) {++buf;}
                if ((buf - start - 1) < 0) ERROR_ENV;
                char *p = malloc((buf - start + 1) * sizeof(char));
                memcpy(p, start, sizeof(char) * (buf - start));
                p[buf - start] = '\0';
                char *env = NULL;
                if ((env = getenv(p)) == NULL) {perror("getenv()"); free(p); return false;}

                *args = malloc((strlen(env) + 1) * sizeof(char));
                memcpy(*args, env, strlen(env) + 1);
                commands->cnt++;
                args++;
                free(p);
                break;

            case '\'': 
            case '\"':
                if (waitCommand) ERROR_EMPTY("\033[1;31mError:\033[0m String parameter should be used after a command!\n");
                start = buf++;
                while ((*buf != '\n') && (*buf != *start)) { buf++; }
                if ((*buf == '\n') || (buf - start - 1 < 0)) ERROR_STR;
                *args = malloc(sizeof(char) * (buf - start));
                memcpy(*args, start + 1, sizeof(char) * (buf - start - 1));
                (*args)[buf - start - 1] = '\0';
                commands->cnt++;
                args++;
                buf++;
                break;

            default :
                start = buf;
                while ((*buf != '\n') && (!isspace(*buf)) && (*buf != '|') && (*buf != '<') && (*buf != '>')) {++buf;}
                
                *args = malloc(sizeof(char) * (buf - start + 1)); //注意'\0'
                char *addr = *args;
                args++;

                //一定是指令名
                if (waitCommand) {
                    //后续可以是特殊字符
                    waitCommand = 0;
                    //获取指令名
                    commands->cmd = malloc(sizeof(char) * (buf - start + 1));
                    addr = commands->cmd;
                }
                //无论是参数还是指令名都需要保存
                memcpy(addr, start, sizeof(char) * (buf - start));
                addr[buf - start] = '\0'; //指令的每部分都要以\0结尾
                commands->cnt++;
                break;
        }
    }
}

void forkToExecute(Command *command, int fd_in, int fd_out) {
    //shell-builtin
    if (strcmp(command->cmd, "exit") == 0) {printf("\033[1;36mGood Bye!\033[0m\n");freeCommand(command);exit(0);}
    else if (strcmp(command->cmd, "cd") == 0) {
        callCd(command);
        return;
    }
    //单指令无参数时默认输出到标准输出
    //单指令多个参数时进行重定向操作
    //多指令后续创建子进程处理
    else if (strcmp(command->cmd, "history") == 0 && commandsCount == 1) {
        callHistory(command, history);
        return ;
    }
    //无参数时默认重定向至less进行显示
    //有重定向时重定向至相应文件
    //多指令则后续创建子进程处理
    else if (strcmp(command->cmd, "help") == 0 && commandsCount == 1) {
        callHelp(command); 
        return ;
    }
    else if (strcmp(command->cmd, "set") == 0 && commandsCount == 1) {
        callSet(command);
        return;
    }
    else if (strcmp(command->cmd, "unset") == 0) {
        callUnset(command);
        return;
    }
    else if (strcmp(command->cmd, "umask") == 0) {
        callUmask(command);
        return;
    }
    //else if (strcmp(command->cmd, "jobs") == 0) {
    //    callJobs(command);
    //    return;
    //}
    //else if (strcmp(command->cmd, "fg") == 0) {
    //    callFg(command);
    //    return;
    //}
    //else if (strcmp(command->cmd, "bg") == 0) {
    //    callBg(command);
    //    return;
    //}
    //else if (strcmp(command->cmd, "test") == 0) {
    //    callTest(command);
    //    return;
    //}

    pid_t pid = fork();
    if (pid < 0) ERROR_FORK;
    else if (pid == 0) { //child
        if (command->read) {
            int in = open(command->read, O_RDONLY, 0666);
	        if (in < 0) ERROR_OPEN;
            dup2(in, 0);
            close(in);
        } 
        else if (fd_in > 0) {
            dup2(fd_in, 0);
        }

        if (command->write) {
            int out = open(command->write, O_WRONLY | O_TRUNC | O_CREAT, 0666);
            if (out < 0) ERROR_OPEN;
            dup2(out, 1);
            close(out);
        } 
        else if (command->overwrite) {
            int append = open(command->overwrite, O_WRONLY | O_CREAT | O_APPEND, 0666);
            if (append < 0) ERROR_OPEN;
            dup2(append, 1);
            close(append);
        } 
        else if (fd_out > 0) {
            dup2(fd_out, 1);
        }

        //单独处理history
        if (strcmp(command->cmd, "history") == 0) {callHistory(command, history); exit(0);}
        //单独处理help
        if (strcmp(command->cmd, "help") == 0) {printHelp(); exit(0); }
        //单独处理set
        if (strcmp(command->cmd, "set") == 0) {printEnv(); exit(0); }

        if((status = execvp(command->cmd, command->args)) < 0) ERROR_EXECUTE(command->cmd);  

    } 
    else { //parent
        wait(&status);
        if (!(WIFEXITED(status))) ERROR_EXIT;
    }
}

void executeCommands(Command *commands) {
    if (commandsCount == 1) {
        forkToExecute(commands, -1, -1);
        freeCommand(commands);
    }
    else if (commandsCount == 2) {
        int fd[2];
        pipe(fd);
        forkToExecute(commands, -1, fd[1]);
        close(fd[1]);
        freeCommand(commands++);
        forkToExecute(commands, fd[0], -1);
        close(fd[0]);
        freeCommand(commands);
    } 
    else {
        int *pipes[2];
        pipes[0]= malloc(sizeof(int) * 2);
        pipes[1]= malloc(sizeof(int) * 2);
        int newPoint = 0;
        pipe(pipes[newPoint]);
        forkToExecute(commands, -1, (pipes[newPoint])[1]);
        close((pipes[newPoint])[1]);
        freeCommand(commands++);

        for (int i = 1; i < (commandsCount - 1); ++i) {
            newPoint = 1 - newPoint;
            pipe(pipes[newPoint]);
            forkToExecute(commands,(pipes[1-newPoint])[0],(pipes[newPoint])[1]);
            close((pipes[1-newPoint])[0]);
            close((pipes[newPoint])[1]);
            freeCommand(commands++);
        }

        forkToExecute(commands,(pipes[newPoint])[0],-1);
        close((pipes[newPoint])[0]);
        freeCommand(commands);
    }
    return;
}

/* 1.打印命令提示符
 * 2.路径会跟随实际路径变化
 * 3.彩色显示
 * */
void prompt() {
    char *path = NULL;
    char host[1024];
    struct passwd *pwd; //保存用户账户信息的结构体
    pwd = getpwuid(getuid()); //通过uid来获取用户信息结构体
    path = getcwd(NULL, 0); //getcwd获取到的内容是存放在堆的
    gethostname(host, 1024);
    printf("\033[1;34m%s%s%s\033[0m%s\033[1;36m%s\033[0m $ ", pwd->pw_name, "@", host, ":", path); //彩色显示
    free(path); //获取完就释放内存
}

int main () {
	printf("\033[1;33m-------------------------------------------------\n");
	printf("\033[5;33m|   	    Welcome to zs's shell :)		|\n");
	printf("\033[1;33m-------------------------------------------------\n");
    setenv("SHELL", "/bin/zsh", 1);
    while (1) {
        prompt();
        if (fgets(buf, BUF_SIZE, stdin) != NULL) {
            saveHistory(buf);
            Command *cmds = malloc(MAX_COMMANDS * sizeof(Command));
            commandsCount = 1;
            if (splitCommands(buf, cmds)) executeCommands(cmds);
            free(cmds);
        }
    }
}
