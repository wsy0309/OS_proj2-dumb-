#define FALSEE 0
#define TRUE 1

#include<stdio.h>
#include<stdlib.h>

typedef struct L2Page{
	int valid;
	unsigned int baseAddr;
	unsigned int* page;
}L2page;

typedef struct L1Page{
	int valid;
	unsigned int baseAddr;
	L2Page* L2PT;
}L1Page;

typedef struct Pcb{
	int pid;
	int remain_time_quantum;
	int remain_io_time;
	int remain_cpu_time;
	
	L1Page* L1PT;
}Pcb;

typedef struct ProcqNode{
	struct Pcb* pcb;
	struct ProcqNode* next;
}ProcqNode;

typedef struct Procq{
	int count;
	ProcqNode* head;
	ProcqNode* pos;
}Procq;

Procq* createProcq(){
	Procq* pnew = (Procq*)malloc(sizeof(Procq));
	pnew->count = 0;
	pnew->head = NULL;
	pnew->pos = NULL;
	return pnew;
}

//해당 pcb가 list에 존재하는지 search
int _searchProcq(Procq* p, ProcqNode** pPre, ProcqNode** pLoc, Pcb* pcb){
	for(*pPre=NULL,*pLoc = p->head; *pLoc != NULL; *pPre = *pLoc, *pLoc = (*pLoc)->next){
		if((*pLoc)->pcb->pid == pcb->pid)
			return TRUE;
//		else if((*pLoc)->pcb > pcb)
//			break;
	}
	return FALSE;
	}

//addProcq내에서 쓰임
void _insertProcq(Procq*p, ProcqNode* pPre, ProcqNode* pLoc, Pcb* pcb){
	ProcqNode* pnew = (ProcqNode*)malloc(sizeof(ProcqNode));
	pnew->pcb = pcb;
	if(pPre == NULL){
		pnew->next = p->head;
		p->head = pnew;
	}
	else{
		pnew->next = pPre->next;
		pPre->next = pnew;
	}
	p->count++;

}
//RemoveProcq내에서 쓰임
Pcb* _deleteProcq(Procq*p, ProcqNode* pPre, ProcqNode* pLoc){
	Pcb* temp = NULL;
	temp = pLoc->pcb;
	if(pPre == NULL)
		p->head = pLoc->next;
	else
		pPre->next = pLoc->next;
	free(pLoc);
	p->count--;
	return temp;
}

//해당 pcb존재하는지 확인후 Procq에 add
void AddProcq(Procq*p, Pcb* pcb){
	ProcqNode* pPre = NULL, *pLoc = NULL;
	int found;
	found = _searchProcq(p, &pPre, &pLoc, pcb);
	if(!found)
		_insertProcq(p, pPre, pLoc, pcb);
}


//해당 pcb존재하는지 확인후 Procq에서 remove
Pcb* RemoveProcq(Procq*p, Pcb* pcb){
	Pcb* res = NULL;
	ProcqNode* pPre = NULL, *pLoc = NULL;
	int found;
	found = _searchProcq(p, &pPre, &pLoc, pcb);
	if(found)
		res = _deleteProcq(p, pPre, pLoc);
	return res;
}

void destroyProcq(Procq* p){
	ProcqNode* pdel = NULL, *pnext = NULL;
	for (pdel = p->head;pdel != NULL; pdel = pnext){
		pnext = pdel->next;
	    free(pdel);
	}
	free(p);
}


