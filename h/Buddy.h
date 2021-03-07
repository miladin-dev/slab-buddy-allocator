#pragma once

#include <stdlib.h>
#include <stdbool.h>

#define BLOCK_SIZE (4096)
#define BUDDYOF(bAddr, i) (bAddr ^)
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

union buddy_b {
	unsigned char size[BLOCK_SIZE];
	union buddy_b* next;
}buddy_block;

struct buddy_s {
	union buddy_b* mArr[32];
	int entries_num;
	//.. mozda jos neki info
	int left_over;

}buddy_t;


void* buddy_initialize(void* startaddr, int blocknum);
void buddy_add(void* block_addr, int blockstoadd);
void* buddy_take(int numBlocks);
void* get_buddy(void* baddr, int level);

void buddy_on_level(void* block, int level);
void* split_block(void* b_addr, int lvl, int x);

bool isPwrOfTwo(int x);