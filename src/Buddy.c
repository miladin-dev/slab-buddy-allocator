#pragma once
#include "Buddy.h"
#include <stdio.h>
#include <math.h>

int blocks_num;
void* base_addr;
unsigned addr_align;
unsigned mask = 0xFFFFFFFF;
struct buddy_s* buddy_mngr;
union buddy_b* block_ptr;
typedef union buddy_b b_block;

bool isPwrOfTwo(int x) {
	return (x != 0) && ((x & (x - 1)) == 0);
}

void* buddy_initialize(void* startAddr, int blockNum)
{
	base_addr = startAddr;
	blocks_num = blockNum;

	if (base_addr == NULL || blocks_num <= 1)
		return NULL;
	 
	addr_align = (mask << 12) & (unsigned)base_addr;
	if ((unsigned)base_addr != addr_align) {
		addr_align = (1 << 12) + addr_align;
		base_addr = (void*)addr_align;
		--blocks_num;
	}

	// Allocated for buddy_sys;
	blocks_num--;

	buddy_mngr = base_addr;
	//printf_s("Buddy Manager Address = %p \n", base_addr);
	
	base_addr = (unsigned char*)base_addr + BLOCK_SIZE;		//Base addr points to next block, as first is meant for buddy_mngr.
	 

	int power = (int)log2(blocks_num);		
	buddy_mngr->entries_num = power + 1;	// 128 = 2^7 -> 2^0-7 = 8
	int currEntriesNum = power + 1;
	buddy_mngr->left_over = blocks_num - (1 << power);
	blocks_num = 1 << power;

	for (int i = 0; i < 32; i++) {
		buddy_mngr->mArr[i] = NULL;
	}

	//printf_s("Start Address with alignign = %p \n", base_addr);
	//printf_s("blocks num = %d \n", blocks_num);
	block_ptr = (union buddy_b*)base_addr;

	buddy_add(base_addr, blocks_num);

	return base_addr;
}


void buddy_add(void* block_addr, int blocksToAdd) {
	if (blocksToAdd <= 0) return;
	int level = log2(blocksToAdd);
	if (level >= 32) return;

	if (buddy_mngr->mArr[level] == NULL) {
		buddy_mngr->mArr[level] = block_addr;
		buddy_mngr->mArr[level]->next = NULL;
	}
	else {
		buddy_on_level(block_addr, level);
	}
}

void* buddy_take(int numBlocks)
{
	if (numBlocks == 0) return NULL;

	int level = (int)log2(numBlocks) + !isPwrOfTwo(numBlocks);
	void* ret_addr;

	if (buddy_mngr->mArr[level] != NULL) {			//Ima na mom nivou blok velicine koji mi treba
		ret_addr = buddy_mngr->mArr[level];
		buddy_mngr->mArr[level] = buddy_mngr->mArr[level]->next;
	}
	else {
		int x = level + 1;

		while (x < 32 && buddy_mngr->mArr[x] == NULL && x < buddy_mngr->entries_num) {
			x++;
		}

		if (x >= 32 || x == buddy_mngr->entries_num) return NULL;
		b_block* nextB_on_lvlX = NULL;

		if (buddy_mngr->mArr[x]->next != NULL) {
			nextB_on_lvlX = buddy_mngr->mArr[x]->next;
		}

		void* b_addr = (unsigned char*)buddy_mngr->mArr[x];
		ret_addr = split_block(b_addr, level, x);

		buddy_mngr->mArr[x] = nextB_on_lvlX;
	}

	return ret_addr;
}

void* split_block(void* b_addr, int startLvl, int x)
{
	if (startLvl == x) return b_addr; 

	void* block_to_split = b_addr;
	int blocks_to_add = (1 << (x - 1));

	buddy_add(block_to_split, blocks_to_add);

	void* ret_block = (unsigned char*)block_to_split + (1 << (x - 1)) * BLOCK_SIZE;

	return split_block(ret_block, startLvl, x - 1);
}

void buddy_on_level(void* block, int level) {

	if (buddy_mngr->mArr[level] == NULL || level >= 32)
		return;

	int buddyFound = 0;

	b_block* myBuddy = get_buddy(block, level);		//dobijam koji je moj buddy, ne i da li postoji tu
	
	b_block* temp = buddy_mngr->mArr[level];
	b_block* prev = NULL;

	while (temp) {
		if (myBuddy == temp) {
			//printf("jesu badii \n");
			buddyFound = 1;

			if (prev) {
				prev->next = temp->next;
			}
			else {
				buddy_mngr->mArr[level] = temp->next;
			}

			int blocks_to_add = 1 << (level + 1);
			buddy_add(MIN(block, (void*)myBuddy), blocks_to_add);			//min = pocetna adresa bloka 

			return;
		}
		else {
			prev = temp;
			temp = temp->next;
		}
	}
	if (buddyFound == 0) {
		((b_block*)block)->next = buddy_mngr->mArr[level];
		buddy_mngr->mArr[level] = block;
	}

	return;
}

//Returns address of bAddr buddy.
void* get_buddy(void* bAddr, int level)
{
		int powr2 = 1 << level;			
		unsigned char* new_addr = (unsigned char*)bAddr - (unsigned char*)base_addr;

		//na nultom nivou blok jedan do drugo se razlikuju u 12. bitu, na 1. niviou 2 para po 2 bloka se razlikuju na 13. bitu
		unsigned flipped = ((unsigned)new_addr) ^ (powr2 << 12);
		return ((unsigned)flipped + (unsigned)base_addr);
}


void buddy_print() {
	union buddy_b* temp;

	for (int i = 0; i < buddy_mngr->entries_num; i++) {
		printf_s("Entry - %d -> ", i);
		temp = buddy_mngr->mArr[i];
		while (temp) {
			int num_to_print = (1 << i);
			if (i <= 8) {
				for (int j = 0; j < num_to_print; j++) {
					printf_s("%p|", (unsigned char*)temp + j * (1 << 12));
				}
				printf_s("|");
			}
			else {
				printf_s("%p ", (unsigned char*)temp);
			}
			temp = temp->next;
		}
		printf_s("\n");
	}
}
