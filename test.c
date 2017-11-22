/*********************************************
[ Project 1 ]
RR scheduling policy

cpu_time :  random
io_time  :  random
**********************************************/

#include "procqADT.h" //linked list
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>
#include <time.h>

//msg key
#define QUEUE_KEY 3215
#define VA_KEY 2017

	
struct Pcb* pcbs[10];
struct Pcb* present;
struct msgNode msg, va_msg;

struct Procq* runq;
struct Procq* waitq;

int global_tick = 0;
int cpu_time[10];
int remain_cpu_time;
int remain_io_time;
int minfree = 0;
int maxfree = 100000;

void PrintQueue(Procq *q);
void pAlarmHandler(int signo);
void cAlarmHandler(int signo);
void updateWaitq();
void child_action(int cpu_time);
void io_action();
void searchShort();
Pcb* scheduler();
void createVA();
void addr_translator(msgNode msg);
void checkfree();

key_t msgpid[2];

int main(){
	int pid, i;
	struct sigaction old_sa, new_sa;
	struct itimerval new_timer, old_timer;
	Pcb* next = NULL;
	
	runq = (Procq*)malloc(sizeof(Procq));
	waitq = (Procq*)malloc(sizeof(Procq));
	runq = createProcq();
	waitq = createProcq();
		
	//create random value
	srand((int)time(NULL));
	for(i = 0; i<10; i++){
		cpu_time[i] = (rand() % 10) + 1;
		//printf("create cpu_time : %d\n",cpu_time[i]);	
	}

	for(i = 0; i<10; i++){
		pid = fork();
		if(pid < 0){
			printf("fork error\n");
			exit(0);
		}
		else if(pid == 0){
			//child
			child_action(cpu_time[i]);
			exit(0);
		}
		else{
			//parent
			pcbs[i] = (Pcb*)malloc(sizeof(Pcb));
			memset(pcbs[i],0,sizeof(Pcb));
			pcbs[i]->pid = pid;
			pcbs[i]->remain_cpu_time = cpu_time[i];
			pcbs[i]->remain_time_quantum = 2;
			pcbs[i]->L1_PT = NULL;
			AddProcq(runq, pcbs[i]);
		}
	}
	
	memset(&new_sa,0,sizeof(sigaction));
	new_sa.sa_handler = &pAlarmHandler;
	sigaction(SIGALRM, &new_sa, &old_sa);

	//timer
	new_timer.it_value.tv_sec = 1;
	new_timer.it_value.tv_usec = 0;
	new_timer.it_interval.tv_sec = 1;
	new_timer.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &new_timer, &old_timer);

	if((msgpid[0] = msgget((key_t)QUEUE_KEY, IPC_CREAT|0644)) == -1){
		printf("msgget error \n");
		exit(0);
	}
	printf("msg (key : %d, id : %d) created\n",QUEUE_KEY, msgpid[0]);

	if((msgpid[1] = msgget((key_t)VA_KEY, IPC_CREAT|0644)) == -1){
        printf("msgget error \n");
        exit(0);
    }
    printf("msg (key : %d, id : %d) created\n",VA_KEY, msgpid[1]);

	while(1){
		if(msgpid[0] > 0){
		//receive msg
			if((msgrcv(msgpid[0], &msg, (sizeof(msg) - sizeof(long)), 0, 0)) > 0){
				for(i = 0; i<10; i++){
					if(pcbs[i]->pid == msg.pid){
						pcbs[i]->remain_io_time = msg.io_time;
						pcbs[i]->remain_time_quantum = 2;
						RemoveProcq(runq, pcbs[i]);
						AddProcq(waitq, pcbs[i]);
						printf("global_tick (%d) proc(%d) sleep (%d) ticks\n", global_tick, pcbs[i]->pid, pcbs[i]->remain_io_time);
					}
				}
				next = scheduler();
				if(next != NULL){
					present = next;
					printf("global_tick(%d) schedule proc(%d)\n",global_tick, present->pid);
				}
				
			}
		}
		if((msgrcv(msgpid[1], &va_msg, (sizeof(va_msg) - sizeof(long)), 0, 0)) > 0){
			addr_translator(va_msg);
		}		
	}
	exit(0);
}
/*********************************************
 pAlarmHandler : parent signal handler
 - 종료: 일정 tick발생 후 child & parent kill
 - update time quantum : 업데이트후 AddProq
 - 자식 프로세서에 signal 보냄
**********************************************/
void pAlarmHandler(int signo){
	Pcb* next = NULL;
	global_tick++;

	if(global_tick >= 10){
		for(int i = 0; i<10; i++){
			printf("parent killed child)(%d)\n",pcbs[i]->pid);
			kill(pcbs[i]->pid, SIGKILL);		
		}
		kill(getpid(), SIGKILL);
		exit(0);
	}

	//waitq update
	if(waitq->count != 0)
		updateWaitq();
	if(present == NULL){
		present = scheduler();
		printf("global_tick(%d) schedule proc(%d)\n",global_tick, present->pid);
	}
	else{
		present->remain_time_quantum--;
		if(present->remain_time_quantum == 0){
			present->remain_time_quantum = 2;
			RemoveProcq(runq, present);
			AddProcq(runq, present);
			if((next = scheduler()) != NULL){
				present = next;
				printf("global_tick(%d) schedule proc(%d)\n",global_tick, present->pid);
			}
		}	
	}
	printf("runq : ");
	PrintQueue(runq);
	printf("waitq : ");
	PrintQueue(waitq);

	kill(present->pid, SIGALRM);
}
/************************************************
 cAlarmHandler : child signal handler
 - update remain_cpu_time
 - io_action : remain_cpu_time == 0 인경우
*************************************************/

void cAlarmHandler(int signo){
	printf("proc(%d) remain_cpu_time : %d\n",getpid(), remain_cpu_time);
	createVA();
	remain_cpu_time--;
	if(remain_cpu_time == 0){
		io_action();
	}
	return;
}
/*********************************************
 updateWaitq : update remain_io_time
**********************************************/

void updateWaitq(){
	ProcqNode* cur = NULL, *next = NULL;
	Pcb* tmpPcb = (Pcb*)malloc(sizeof(Pcb));
	tmpPcb = NULL;
	if(waitq->count != 0){
		for(cur = waitq->head; cur!=NULL; cur=next){
			next = cur->next;
			tmpPcb = cur->pcb;
			cur->pcb->remain_io_time--;
			if(cur->pcb->remain_io_time == 0){
				RemoveProcq(waitq, tmpPcb);
				AddProcq(runq,tmpPcb);
			}		
		}
	}
}

/************************************************
 child_action : initial signal action
*************************************************/

void child_action(int cpu_time){
	struct sigaction old_sa, new_sa;
	
	remain_cpu_time = cpu_time;

	memset(&new_sa,0,sizeof(struct sigaction));
	new_sa.sa_handler = &cAlarmHandler;
	sigaction(SIGALRM, &new_sa, &old_sa);

	msgpid[0] = (key_t)malloc(sizeof(key_t));
	msgpid[0] = -1;

	while(1){
	}
}
/**********************************************
 io _action : send msg to parent
**********************************************/
void io_action(){
	int mspid, ret;
	printf("child (%d) send msg\n",getpid());
	if((mspid = msgget((key_t)QUEUE_KEY, IPC_CREAT|0644)) == -1){
		printf("msgget error \n");
		exit(0);
	}
	memset(&msg, 0, sizeof(msg));
	msg.pid = getpid();
	msg.io_time = (rand() % 5) + 1;
	msg.cpu_time = (rand() % 10) + 1;
 	msg.msgType = 1;
	remain_cpu_time = msg.cpu_time;
	
	if((msgsnd(mspid, &msg, (sizeof(msg) - sizeof(long)), IPC_NOWAIT)) == -1){
		printf("msgsnd error \n");
		exit(0);
	}
}
/******************************************
 scheduler : select process
 - RR
******************************************/

Pcb* scheduler(){
	//RoundRobin
	if(runq->count != 0)
		return runq->head->pcb;
	else
		return NULL;
}

void PrintQueue(Procq* q){
	ProcqNode* tmp = (ProcqNode*)malloc(sizeof(ProcqNode));
 	tmp = q->head;
	if(tmp == NULL){
		printf("-\n");
		return;
	}
	do{
		printf("%d  ",tmp->pcb->pid);
		tmp = tmp->next;
	}while(tmp != NULL);
	("\n");
}

void createVA(){
	int mspid;
	if((mspid = msgget((key_t)VA_KEY, IPC_CREAT|0644)) == -1){
        printf("msgget error \n");
        exit(0);
    }
    memset(&va_msg, 0, sizeof(va_msg));
    va_msg.pid = getpid();
	va_msg.msgType = 1;
	for(int i = 0; i<10; i++){
		va_msg.va[i] = (unsigned int)(rand() % 100000000);
		printf("VA : %u\n", va_msg.va[i]);
	}

	if((msgsnd(mspid, &va_msg, (sizeof(va_msg) - sizeof(long)), IPC_NOWAIT)) == -1){
        printf("msgsnd error \n");
        exit(0);
    }
}

void addr_translator(msgNode msg){
	unsigned int va=0, pa=0, offset, L2_index, L1_index;

	for(int i = 0; i<10; i++){
		if(pcbs[i]->pid == msg.pid){
			for(int j = 0; j<10;j++){
				va = msg.va[j];
				offset = va & 0x00000fff;
				L2_index = (va & 0x003ff) >> 12;
				L1_index = (va & 0xffc) >> 22;

				if(pcbs[i]->L1_PT == NULL){
					minfree++;
					checkfree();
					pcbs[i]->L1_PT = (L1_page*)malloc(sizeof(L1_page) * 1024);				
				}
				if(pcbs[i]->L1_PT[L1_index].valid == 0){	
					minfree++;
					checkfree();
					//이상하넹 이거 어찌 해야되는거딩??
					//L1_PT[L1_index] = (L1_page)malloc(sizeof(L1_page));
					pcbs[i]->L1_PT[L1_index].baseAddr = 1000 * (minfree - 1);
					pcbs[i]->L1_PT[L1_index].valid = 1;	
					pcbs[i]->L1_PT[L1_index].L2_PT = (L2_page*)malloc(sizeof(L2_page) * 1024);
		
				}
				if(pcbs[i]->L1_PT[L1_index].L2_PT[L2_index].valid == 0){	
					minfree++;
					checkfree();
					pcbs[i]->L1_PT[L1_index].L2_PT[L2_index].baseAddr = 1000 * (minfree - 1);
					pcbs[i]->L1_PT[L1_index].L2_PT[L2_index].valid = 1;
					pcbs[i]->L1_PT[L1_index].L2_PT[L2_index].page = (Page*)malloc(sizeof(Page) * 1024);
				}
				
				if(pcbs[i]->L1_PT[L1_index].L2_PT[L2_index].page[offset].valid == 0){
					pa = (pcbs[i]->L1_PT[L1_index].L2_PT[L2_index].baseAddr << 12) + offset;
					pcbs[i]->L1_PT[L1_index].L2_PT[L2_index].page[offset].pa = pa;
				}
				printf("VA : 0x%08x  =>  ", va);
				printf("PA : 0x%08x\n",pcbs[i]->L1_PT[L1_index].L2_PT[L2_index].page[offset].pa);
			}
			return;
		}
	}	
}

void checkfree(){
	//freepagelist 모두 다쓴후 종료..?
	if(minfree == maxfree){
		for(int i = 0; i<10; i++){
            printf("parent killed child)(%d)\n",pcbs[i]->pid);
            kill(pcbs[i]->pid, SIGKILL);
        }
        kill(getpid(), SIGKILL);
        exit(0);
	}
}


