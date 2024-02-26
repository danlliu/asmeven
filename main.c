/*
 * Overall design:
 * need to allocate pages of memory that get memprotected with +X
 * link each page together at the end
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096

// every page can hold 314 entries
// 312 entries = 4056 B
// after that is a call %rcx (ff d1) for last page or jmp (next page) (0x48 0xbb
// <addr>; ff e3). worst case is 12 B leaves us with 4068 B
// keep last 4B which is the index of this page

void (*FIXUP_FN)();
void *FIRST_PAGE = NULL;
void *LAST_PAGE = NULL;

void *allocate_page() {
  void *page = mmap(NULL, PAGE_SIZE, PROT_EXEC | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  memset(page, 0x90, PAGE_SIZE);
  return page;
}

void *write_comparison(void *addr, int value, int result) {
  // 0  1  2  3  4  5  6  7  8  9  a  b  c
  // 3d VV VV VV VV 75 06 B8 RR 00 00 00 c3
  uint8_t *code = (uint8_t *)addr;
  *code = 0x3d; // cmp opcode
  *(uint32_t *)(code + 1) = value;
  code += 5;
  *code = 0x75;       // jne opcode
  *(code + 1) = 0x06; // jne offset
  code += 2;
  *code = 0xb8;
  *(uint32_t *)(code + 1) = result;
  code += 5;
  *code = 0xc3;
  code += 1;
  return (void *)code;
}

int get_page_index(void *page) { return *((int *)(page + 4092)); }

void set_page_index(void *page, int index) { *((int *)(page + 4092)) = index; }

void populate_page(void *page, int index) {
  set_page_index(page, index);
  // we have 312 entries per so every first one will always be even
  int value = index * 312;
  int is_even = 1;
  for (int i = 0; i < 312; ++i) {
    page = write_comparison(page, value + i, is_even);
    is_even = !is_even;
  }
  // put 12 noops for the link
  // call fixup: push rax, rbx, rcx, rdx ; call rcx ; pop rdx, rcx, rbx, rax ; jmp rbx
  uint8_t *code = (uint8_t *)page;
  code += 12;
  code[0] = 0x50;
  code[1] = 0x53;
  code[2] = 0x51;
  code[3] = 0xff;
  code[4] = 0xd1;
  code[5] = 0x59;
  code[6] = 0x5b;
  code[7] = 0x58;
  code[8] = 0xff;
  code[9] = 0xe3;
}

void link_page(void *page, void *next_page) {
  uint8_t *code = (uint8_t *)page;
  code += (312 * 13);
  *code = 0x48; // movabs
  code[1] = 0xbb;
  *(void **)(code + 2) = next_page; // value
  code += 10;
  code[0] = 0xff;
  code[1] = 0xe3;
}

void fixup() {
  void *next_page = allocate_page();
  int index = LAST_PAGE ? get_page_index(LAST_PAGE) + 1 : 0;
  populate_page(next_page, index);
  if (LAST_PAGE) {
    link_page(LAST_PAGE, next_page);
  } else {
    FIRST_PAGE = next_page;
  }
  LAST_PAGE = next_page;
}

int is_even(int x) {
  if (FIRST_PAGE == NULL) {
    fixup();
  }
  __asm__ __volatile__("call *%%rbx\n\t"
                       "leave\n\t"
                       "ret\n\t"
                       :
                       : "c"(&fixup), "b"(FIRST_PAGE), "a"(x));
}

int main() {
  int result;
  result = is_even(2);
  printf("is_even(2) = %d\n", result);
  result = is_even(15);
  printf("is_even(15) = %d\n", result);
  result = is_even(373);
  printf("is_even(373) = %d\n", result);
  result = is_even(1048576);
  printf("is_even(1048576) = %d\n", result);
}
