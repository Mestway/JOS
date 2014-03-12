// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "showmappings", "Display a range of address, method: setmappings va1 va2", mon_showmappings },
	{ "setmapping", "Set a new mapping given va and pa, method: setmapping va pa [perm], pa = 'n' will not update physaddr", mon_setmapping},
	{ "clearmapping", "Clear a mapping given va, method: clearmapping va", mon_clearmapping},
	{ "dump", "Dump a range of memory", mon_dump},
	{ "backtrace", "Back trace", mon_backtrace},
	{ "continue", "Continue from a breakpoint", mon_continue},
	{ "si", "step an instruction", mon_si},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

unsigned string2num(char *str, int base);
int check_num(char c, int base);
static physaddr_t print_va2pa(pde_t *pgdir, uintptr_t va, bool print);
static void set_va(pte_t *pgdir ,uintptr_t va, physaddr_t pa, int perm);
static void print_perm(unsigned perm);
extern void env_run(struct Env *);

// continue
int mon_continue(int argc, char **argv,struct Trapframe* tf)
{
	if(tf == NULL || (tf -> tf_trapno != T_BRKPT && tf->tf_trapno != T_DEBUG) ) {
		cprintf("You are not in a break point, nothing to continue.\n");
		return 0;
	}
	cprintf("recover now!\n");

	extern struct Env *curenv;
	tf->tf_eflags &= ~FL_TF;
	env_run(curenv);

	return 0;
}

int mon_si(int argc, char **argv, struct Trapframe* tf)
{
	if(tf == NULL || (tf -> tf_trapno != T_BRKPT && tf->tf_trapno != T_DEBUG) ) {
		cprintf("You are not in a break point, nothing to continue.\n");
		return 0;
	}
	cprintf("go one step\n");
	tf->tf_eflags |= FL_TF;
	extern struct Env *curenv;
	env_run(curenv);

	return 0;
}
void
set_boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
{
	// Fill this function in
	// By Stanley Wang
	
	uintptr_t k = va;
	unsigned i = 0;
	unsigned j = pa;
	for(i = 0; i < size / PGSIZE; i ++) {
		pte_t *pg_entry = pgdir_walk(pgdir, (void *)k, 1);
		*pg_entry = (j & (~0xfff)) | perm;
		k += PGSIZE;
		j += PGSIZE;
	}
}

//By Stanley Wang
void 
set_extend_map(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm) {
	
	unsigned ex_pgsize = PGSIZE << 10;
	unsigned i = 0; 
	unsigned j = pa;
	unsigned k = va;
	pte_t *pde;
	for(i = 0; i < size / ex_pgsize; i ++) {
		pde = &pgdir[PDX(k)];
		*pde = ((j & (0xffc00000)) | perm | PTE_P | PTE_PS);	
		k += ex_pgsize;
		j += ex_pgsize;
	}
}

// By Stanley Wang
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	//cprintf("showmappings: %s %s\n", argv[1], argv[2]);
	
	unsigned left, right;

	if(argv[1][0] == '0' && argv[1][1] == 'x') {
		left = string2num(argv[1], 16);
	} else {
		left = string2num(argv[1], 10);
	}

	if(argv[2][0] == '0' && argv[2][1] == 'x') {
		right = string2num(argv[2], 16);
	} else {
		right = string2num(argv[2], 10);
	}
	
	uint32_t kern_pgdir =(uint32_t) KADDR((physaddr_t)rcr3());
	
	int i = 0;
	int p = left;
	unsigned size = (right - left) / PGSIZE;
	for(i = 0; i < size; i ++) {
		p += PGSIZE;
		print_va2pa((pde_t *)kern_pgdir, (uintptr_t) p, true);
	}

	return 0;
}

int mon_clearmapping(int argc, char **argv, struct Trapframe *tf)
{	
	unsigned left, right;

	if(argv[1][0] == '0' && argv[1][1] == 'x') {
		left = string2num(argv[1], 16);
	} else {
		left = string2num(argv[1], 10);
	}

	uint32_t kern_pgdir =(uint32_t) KADDR((physaddr_t)rcr3());
	set_va((pte_t *)kern_pgdir, (uintptr_t)left, (physaddr_t)0, 0);
	
	print_va2pa((pte_t *)kern_pgdir,left, true);
	return 0;
}

int
mon_setmapping(int argc, char **argv, struct Trapframe *tf)
{	
	unsigned left, right = 0;
	bool right_exist = true;

	if(argv[1][0] == '0' && argv[1][1] == 'x') {
		left = string2num(argv[1], 16);
	} else {
		left = string2num(argv[1], 10);
	}

	if(argv[2][0] == '0' && argv[2][1] == 'x') {
		right = string2num(argv[2], 16);
	} else if(argv[2][0] == 'n') {
		right_exist = false;
	}else {
		right = string2num(argv[2], 10);
	}

	unsigned perm = 0;
	if(argv[3] != NULL) {
		perm = string2num(argv[3],2);
	}

	uint32_t kern_pgdir =(uint32_t) KADDR((physaddr_t)rcr3());

	if(right_exist == false) {
		right = print_va2pa((pte_t *)kern_pgdir, left, false);	
	}

	set_va((pte_t *)kern_pgdir, (uintptr_t)left, (physaddr_t)right, PTE_P | perm);
	print_va2pa((pte_t *)kern_pgdir,left, true);

	return 0;
}

int mon_dump(int argc, char **argv,struct Trapframe *tf) {

	//cprintf("showmappings: %s %s\n", argv[1], argv[2]);
	
	unsigned left, right;

	if(argv[1][0] == '0' && argv[1][1] == 'x') {
		left = string2num(argv[1], 16);
	} else {
		left = string2num(argv[1], 10);
	}

	if(argv[2][0] == '0' && argv[2][1] == 'x') {
		right = string2num(argv[2], 16);
	} else {
		right = string2num(argv[2], 10);
	}
	
	uint32_t kern_pgdir =(uint32_t) KADDR((physaddr_t)rcr3());
	
	int i = 0;
	int p = left;
	unsigned size = (right - left) / PGSIZE;
	for(i = 0; i < size; i ++) {
		physaddr_t pa = print_va2pa((pde_t *)kern_pgdir, (uintptr_t) p, false);
		if(pa == ~0) cprintf("Page not exist\n");
		else {
			int t = 0;
			for(t = 0; t < PGSIZE; t ++) {
				cprintf("0x%x ", *(unsigned *)(p));
			}
			cprintf("\n");
		}
	}
	return 0;
}

static void
set_va(pte_t *pgdir ,uintptr_t va, physaddr_t pa, int perm) {
	if(perm & PTE_PS)
		set_extend_map(pgdir, va, PGSIZE, pa, perm);
	else 
		set_boot_map_region(pgdir, va, PGSIZE, pa, perm);
}

static physaddr_t
print_va2pa(pde_t *pgdir, uintptr_t va, bool print)
{
	pte_t *p;

	pgdir = &pgdir[PDX(va)];
	if (!(*pgdir & PTE_P)) {
		if(print) cprintf("Page does not exist at address 0x%x\n", va);
		return ~0;
	}
	if((*pgdir) & PTE_PS) {
		if(print) cprintf("0x%x:0x%x ",va, ((0xffc00000) & *pgdir) | (va & 0x3fffff)); 
		if(print) print_perm(*pgdir);
		return (0xffc00000 & *pgdir);
	}
	
	p = (pte_t*) ((0xf0000000) | (PTE_ADDR(*pgdir)));
	if (!(p[PTX(va)] & PTE_P)){
		if(print) cprintf("Page does not exist at address 0x%x\n", va);
		return ~0;
	}
	
	if(print) cprintf("0x%x:0x%x ",va, PTE_ADDR(p[PTX(va)]) | (va & 0xfff));
	if(print) print_perm(p[PTX(va)]);
	return PTE_ADDR(p[PTX(va)]);	
}

static void print_perm(unsigned perm) {
	
	cprintf("[");
	if(perm & PTE_P) cprintf(" PTE_P");
	if(perm & PTE_W) cprintf(" PTE_W");
	if(perm & PTE_U) cprintf(" PTE_U");
	if(perm & PTE_PWT) cprintf(" PTE_PWT");
	if(perm & PTE_PCD) cprintf(" PTE_PCD");
	if(perm & PTE_A) cprintf(" PTE_A");
	if(perm & PTE_D) cprintf(" PTE_D");
	if(perm & PTE_PS) cprintf(" PTE_PS");
	if(perm & PTE_G) cprintf(" PTE_G");
	cprintf(" ]\n");
}

unsigned
string2num(char *str, int base) {
	unsigned ret = 0;
	int len = strlen(str);
	int i = 0;
	if(base == 16){
		i = 2;
	} else i = 0;
	for(; i < len; i ++) {
		ret *= base;
		ret += check_num(str[i],base);
	}
	return ret;
}

int check_num(char c, int base) {
	if(base == 10) {
		assert(c >= '0' && c <= '9');
		return c - '0';
	}
	else if(base == 16) {
		assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
		if(c >= '0' && c <= '9') {
			return c - '0';
		} else {
			switch(c) {
				case 'a': return 10;
				case 'b': return 11;
				case 'c': return 12;
				case 'd': return 13;
				case 'e': return 14;
				case 'f': return 15;
			}
		}
	} else if(base == 2) {
		assert(c >= '0' && c <= '1');
		return c - '0';	
	}
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.	
	uint32_t args[5];
	uint32_t ret_addr;
	uint32_t ebp = read_ebp();
	
	while(1) {
		ret_addr = *((uint32_t *)ebp + 1);
		
		uint32_t i = 2;
		for(i = 2; i <= 6; i++) {
			args[i - 2] = *((uint32_t *) ebp + i);
		}
		
		cprintf("ebp %08x  eip %08x  args", ebp, ret_addr);
		for(i = 0; i <=4; i ++) {
			cprintf(" %08x", args[i]);
		}
		cprintf("\n");
	
		struct Eipdebuginfo info;
		debuginfo_eip(ret_addr,&info);

		cprintf("     %s:%d: ",info.eip_file,info.eip_line);
		cprintf("%.*s+%d\n", info.eip_fn_namelen, info.eip_fn_name, -info.eip_fn_addr+ret_addr);

		ebp = *(uint32_t *)ebp;
		if(ebp == 0)
			break;
	}
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
