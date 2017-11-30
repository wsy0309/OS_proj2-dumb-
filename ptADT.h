#include "procqADT.h"

L2PT* createL2PT(){
	L2PT* pnew = (L2PT*)malloc(sizeof(L2PT));
	pnew->count = 0;
	pnew->head = NULL;
	pnew->pos = NULL;
	return pnew;
}

//해당 pcb가 list에 존재하는지 search
int _searchL2PT(L2PT* p, L2Page** pPre, L2Page** pLoc, L2Page* L2page){
	for(*pPre=NULL,*pLoc = p->head; *pLoc != NULL; *pPre = *pLoc, *pLoc = (*pLoc)->next){
		if((*pLoc)->page_pfn == L2page->page_pfn)
			return TRUE;
//		else if((*pLoc)->pcb > pcb)
//			break;
	}
	return FALSE;
	}

//addL2PT내에서 쓰임
void _insertL2PT(L2PT*p, L2Page* pPre, L2Page* pLoc, L2Page* L2page){
	L2Page* pnew = (L2Page*)malloc(sizeof(L2Page));
	pnew->page_pfn = L2page->page_pfn;
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
//RemoveL2PT내에서 쓰임
L2Page* _deleteL2PT(L2PT*p, L2Page* pPre, L2Page* pLoc){
	L2Page* temp = NULL;
	temp = pLoc;
	if(pPre == NULL)
		p->head = pLoc->next;
	else
		pPre->next = pLoc->next;
	free(pLoc);
	p->count--;
	return temp;
}

//해당 pcb존재하는지 확인후 L2PT에 add
void AddL2PT(L2PT*p, L2Page* L2page){
	L2Page* pPre = NULL, *pLoc = NULL;
	int found;
	found = _searchL2PT(p, &pPre, &pLoc, L2page);
	if(!found)
		_insertL2PT(p, pPre, pLoc, L2page);
}


//해당 pcb존재하는지 확인후 L2PT에서 remove
int RemoveL2PT(L2PT*p, L2Page* L2page){
	int res = NULL;
	L2Page* pPre = NULL, *pLoc = NULL;
	int found;
	found = _searchL2PT(p, &pPre, &pLoc, L2page);
	if(found)
		res = _deleteL2PT(p, pPre, pLoc);
	return res;
}

void destroyL2PT(L2PT* p){
	L2Page* pdel = NULL, *pnext = NULL;
	for (pdel = p->head;pdel != NULL; pdel = pnext){
		pnext = pdel->next;
	    free(pdel);
	}
	free(p);
}
