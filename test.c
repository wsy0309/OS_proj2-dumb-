/***********************************************
RR scheduling policy

time_quantum :  2
cpu_time     :  random
io_time      :  random
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
#define QUEUE_KEY 3333

typedef struct msgNode{
	long msgType;
	int pid;
	int io_time;
	int cpu_time;
	
	unsigned int vaddr[10];
}msgNode;

struct Pcb* pcbs[10];
struct Pcb* present;
struct msgNode msg;

struct Procq* runq;
struct Procq* waitq;

int global_tick = 0;
int cpu_time[10];
int remain_cpu_time;
int remain_io_time;

int free_min = 0;
int free_max = 1000000;

void PrintQueue(Procq *q);
void pAlarmHandler(int signo);
void cAlarmHandler(int signo);
void updateWaitq();
void child_action(int cpu_time);
void io_action();
void searchShort();
void checkfree();
Pcb* scheduler();
unsigned int addrTranslator(L1Page** L1PT, unsigned int VA);

key_t msgpid;

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
		printf("create cpu_time : %d\n",cpu_time[i]);	
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

	if((msgpid = msgget((key_t)QUEUE_KEY, IPC_CREAT|0644)) == -1){
		printf("msgget error \n");
		exit(0);
	}
	printf("msg (key : %d, id : %d) created\n",QUEUE_KEY, msgpid);

	while(1){
		//메세지 종류에따라 io action 처리를할지
		//address translation을 할지 
		//PA = addrTranslater(현재실행 프로세스의 L1PT, 메세지에서 받은 VA)
		
		if(msgpid > 0){
		//receive msg
			if((msgrcv(msgpid, &msg, (sizeof(msg) - sizeof(long)), 0, 0)) > 0){
				if (msg.msgType == 1){
					for(i = 0; i<10; i++){
						if(pcbs[i]->pid == msg.pid){
							pcbs[i]->remain_io_time = msg.io_time;
							pcbs[i]->remain_cpu_time = msg.cpu_time;
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
				else if(msg.msgType == 2){
                                	int i;
                                	unsigned int PA;
                                	for(i=0; i<10; i++){
                                        	PA = addrTranslator(&runq->head->pcb->L1PT,msg.vaddr[i]);
                                        	printf("VA :0x%08x -> PA :0x%08x\n",msg.vaddr[i], PA);
                               		}
				}
			}
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
	int mspid;	
	
	if((mspid = msgget((key_t)QUEUE_KEY, IPC_CREAT|0644)) == -1){
                printf("msgget error \n");
                exit(0);
                }   
	
	printf("proc(%d) remain_cpu_time : %d\n",getpid(), remain_cpu_time);
	remain_cpu_time--;
	
	if(remain_cpu_time == 0){
		//io_action();
		memset(&msg, 0, sizeof(msg));
        	msg.pid = getpid();
        	msg.io_time = (rand() % 5) + 1;
        	msg.cpu_time = (rand() % 10) + 1;
       		 msg.msgType = 1;
        	remain_cpu_time = msg.cpu_time;
	}
	else{
		//여기서??가상 주소 메세지 보내기
		msg.pid= getpid();
		msg.io_time = 0;
		msg.msgType = 2;
		
		int i;	
		for(i=0; i<10; i++){
			msg.vaddr[i] = rand();
		}
	}
	if((msgsnd(mspid, &msg, (sizeof(msg) - sizeof(long)), IPC_NOWAIT)) == -1){
                printf("msgsnd error \n");
                exit(0);
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

	msgpid = (key_t)malloc(sizeof(key_t));
	msgpid = -1;

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
	printf("\n");
}

unsigned int addrTranslator(L1Page** L1PT, unsigned int VA){
	
	unsigned int L1Index = VA >> 22;
	unsigned int L2Index = (VA & 0x003ff000) >> 12;
	unsigned int offset = VA & 0x00000fff;
	
	unsigned int PA = 0;
	int pfn = 0;

	//해당 프로세스의 L1PageTable이 없다면
	if (*L1PT == NULL){
		pfn = free_min;
		free_min++;
		checkfree();

		*L1PT = (L1Page*)malloc((sizeof(L1Page)*1024));
		memset(*L1PT, 0, sizeof(L1Page)*1024);
	} 
	
	//해당 L1PTE에 mapping이 없다면
	if ((*L1PT)[L1Index].valid == 0){
		pfn = free_min;
		free_min++;
		checkfree();

		(*L1PT)[L1Index].L2PT = (L2Page*)malloc((sizeof(L2Page)*1024));
		memset((*L1PT)[L1Index].L2PT, 0, sizeof(L2Page)*1024);
		
		(*L1PT)[L1Index].baseAddr = (0x1000)*pfn;
		(*L1PT)[L1Index].valid = 1;
	}

	//해당 L2PTE에 mapping이 없다면
	if ((*L1PT)[L1Index].L2PT[L2Index].valid == 0){
		pfn = free_min;
		free_min++;
		checkfree();

		(*L1PT)[L1Index].L2PT[L2Index].page = (unsigned int*)malloc((sizeof(unsigned int)*1024));
		memset((*L1PT)[L1Index].L2PT[L2Index].page, 0, sizeof(unsigned int)*1024);

		(*L1PT)[L1Index].L2PT[L2Index].baseAddr = (0x1000)*pfn;
		(*L1PT)[L1Index].L2PT[L2Index].valid = 1;
	}
	
	//PA 계산
	PA = (*L1PT)[L1Index].L2PT[L2Index].baseAddr|offset;
	//printf("VA :0x%08x -> PA :0x%08x",VA, PA);

	return PA;
	
}


void checkfree(){
    //freepagelist 모두 다쓴후 종료..?
    if(free_min == free_max){
        for(int i = 0; i<10; i++){
            printf("parent killed child)(%d)\n",pcbs[i]->pid);
            kill(pcbs[i]->pid, SIGKILL);
        }
        kill(getpid(), SIGKILL);
        exit(0);
    }
}


