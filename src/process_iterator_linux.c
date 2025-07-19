/**
 *
 * cpulimit - a CPU limiter for Linux
 *
 * Copyright (C) 2005-2012, by:  Angelo Marletta <angelo dot marletta at gmail dot com> 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <sys/vfs.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Hash table for fast process name lookup
#define EXCLUDE_HASH_SIZE 256
struct exclude_entry {
	char *name;
	struct exclude_entry *next;
};
static struct exclude_entry *exclude_hash[EXCLUDE_HASH_SIZE];
static int exclusion_list_loaded = 0;

// Simple hash function for process names (djb2 algorithm)
static unsigned int hash_process_name(const char *name) {
	unsigned int hash = 5381;
	while (*name) {
		hash = ((hash << 5) + hash) + *name++;
	}
	return hash % EXCLUDE_HASH_SIZE;
}

// Add process name to hash table
static void add_excluded_process(const char *name) {
	unsigned int hash = hash_process_name(name);
	struct exclude_entry *entry = malloc(sizeof(struct exclude_entry));
	if (entry) {
		entry->name = malloc(strlen(name) + 1);
		if (entry->name) {
			strcpy(entry->name, name);
			entry->next = exclude_hash[hash];
			exclude_hash[hash] = entry;
		} else {
			free(entry);
		}
	}
}

// Check if process name is in hash table
static int is_process_excluded(const char *name) {
	if (!name) return 0; // Safety check for null pointer
	unsigned int hash = hash_process_name(name);
	struct exclude_entry *entry = exclude_hash[hash];
	while (entry) {
		if (strcmp(entry->name, name) == 0) {
			return 1;
		}
		entry = entry->next;
	}
	return 0;
}

// Clean up hash table memory (called on program exit)
static void cleanup_exclusion_list(void) {
	for (int i = 0; i < EXCLUDE_HASH_SIZE; i++) {
		struct exclude_entry *entry = exclude_hash[i];
		while (entry) {
			struct exclude_entry *next = entry->next;
			free(entry->name);
			free(entry);
			entry = next;
		}
		exclude_hash[i] = NULL;
	}
}

// Extract actual program name from Python command line
static const char* extract_python_program_name(const char *cmdline) {
	if (!cmdline) return NULL;
	
	// Find the last argument (tool name) - look for the last space
	const char *last_arg = strrchr(cmdline, ' ');
	if (!last_arg) return NULL;
	
	last_arg++; // Skip space
	if (*last_arg == '\0') return NULL; // Empty argument
	
	// Extract just the tool name (after last slash if path is present)
	const char *tool_name = strrchr(last_arg, '/');
	if (tool_name) {
		tool_name++; // Skip slash
		if (*tool_name == '\0') return NULL; // Empty tool name after slash
	} else {
		tool_name = last_arg;
	}
	
	// Final safety check
	return (*tool_name != '\0') ? tool_name : NULL;
}

static int should_exclude_process(struct process *p, int exclude_interactive)
{
	if (!exclude_interactive) return 0;
	
	// Safety check: ensure command is not null and not empty
	if (p == NULL || p->command == NULL || p->command[0] == '\0') {
		return 0; // don't exclude if we can't determine the command
	}
	
	// Extract just the executable name from command path
	char *cmd_name = NULL;
	char *first_space = strchr(p->command, ' ');
	char *cmd_part = p->command;
	
	// If there's a space, only consider the part before the first space
	if (first_space) {
		char cmd_buffer[256];
		size_t cmd_len = first_space - p->command;
		if (cmd_len >= sizeof(cmd_buffer)) cmd_len = sizeof(cmd_buffer) - 1;
		strncpy(cmd_buffer, p->command, cmd_len);
		cmd_buffer[cmd_len] = '\0';
		cmd_part = cmd_buffer;
	}
	
	// Now extract just the executable name from the path
	char *last_slash = strrchr(cmd_part, '/');
	if (last_slash && last_slash[1] != '\0') {
		cmd_name = last_slash + 1;
	} else {
		cmd_name = cmd_part;
	}
	
	// Safety check: ensure cmd_name is valid
	if (cmd_name == NULL || cmd_name[0] == '\0') {
		return 0; // don't exclude if command name is invalid
	}
	
	// Minimal fallback list for when no config file is available
	static const char *default_excluded_procs[] = {
		"bash", "sh", "ssh", "sshd", "systemd", "init", "cpulimit", NULL
	};

	// Load exclusion list from config file or use defaults
	if (!exclusion_list_loaded) {
		// Initialize hash table
		memset(exclude_hash, 0, sizeof(exclude_hash));
		
		FILE *config_file = fopen("/etc/cpulimit/exclude.conf", "r");
		
		if (config_file) {
			// Load from config file
			char line[256];
			while (fgets(line, sizeof(line), config_file)) {
				// Remove newline and comments
				char *comment = strchr(line, '#');
				if (comment) *comment = '\0';
				
				// Trim whitespace
				char *start = line;
				while (*start && (*start == ' ' || *start == '\t')) start++;
				char *end = start + strlen(start) - 1;
				while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
				*(end + 1) = '\0';
				
				// Skip empty lines and add to hash table
				if (*start) {
					add_excluded_process(start);
				}
			}
			fclose(config_file);
		} else {
			// Use default list
			for (int i = 0; default_excluded_procs[i] != NULL; i++) {
				add_excluded_process(default_excluded_procs[i]);
			}
		}
		exclusion_list_loaded = 1;
	}
	
	// Check if this process should be excluded using hash table lookup
	if (is_process_excluded(cmd_name)) {
		return 1; // exclude this process
	}
	
	// Also check for login shells (commands starting with '-')
	if (cmd_name[0] == '-' && strlen(cmd_name) > 1) {
		char *login_shell = cmd_name + 1; // Skip the '-'
		if (is_process_excluded(login_shell)) {
			return 1; // exclude this login shell
		}
	}
	
	// Special handling for Python scripts - extract actual program name
	if (strcmp(cmd_name, "python") == 0 || strcmp(cmd_name, "python3") == 0) {
		const char *python_program = extract_python_program_name(p->command);
		if (python_program && is_process_excluded(python_program)) {
			return 1; // exclude Python monitoring tools
		}
	}
	
	return 0; // don't exclude
}

static int get_boot_time()
{
	int uptime = 0;
	FILE *fp = fopen("/proc/uptime", "r");
	if (fp != NULL)
	{
		char buf[BUFSIZ];
		char *b = fgets(buf, BUFSIZ, fp);
		if (b == buf)
		{
			char *end_ptr;
			double upsecs = strtod(buf, &end_ptr);
			uptime = (int)upsecs;
		}
		fclose(fp);
	}
	time_t now = time(NULL);
	return now - uptime;
}

static int check_proc()
{
	struct statfs mnt;
	if (statfs("/proc", &mnt) < 0)
		return 0;
	if (mnt.f_type!=0x9fa0)
		return 0;
	return 1;
}

int init_process_iterator(struct process_iterator *it, struct process_filter *filter)
{
	if (!check_proc()) {
		fprintf(stderr, "procfs is not mounted!\nAborting\n");
		exit(-2);
	}
	//open a directory stream to /proc directory
	if ((it->dip = opendir("/proc")) == NULL)
	{
		perror("opendir");
		return -1;
	}
	it->filter = filter;
	it->boot_time = get_boot_time();
	return 0;
}

static int read_process_info(pid_t pid, struct process *p)
{
	static char buffer[1024];
	static char statfile[32];
	static char statusfile[32];
	static char exefile[1024];
	p->pid = pid;
	//read stat file
	snprintf(statfile, sizeof(statfile), "/proc/%d/stat", p->pid);
	FILE *fd = fopen(statfile, "r");
	if (fd==NULL) return -1;
	if (fgets(buffer, sizeof(buffer), fd)==NULL) {
		fclose(fd);
		return -1;
	}
	fclose(fd);
	char *token = strtok(buffer, " ");
	int i;
	for (i=0; i<3; i++) token = strtok(NULL, " ");
	p->ppid = atoi(token);
	for (i=0; i<10; i++)
		token = strtok(NULL, " ");
	p->cputime = atoi(token) * 1000 / HZ;
	token = strtok(NULL, " ");
	p->cputime += atoi(token) * 1000 / HZ;
	for (i=0; i<7; i++)
		token = strtok(NULL, " ");
	p->starttime = atoi(token) / sysconf(_SC_CLK_TCK);
	
	//read status file to get UID
	snprintf(statusfile, sizeof(statusfile), "/proc/%d/status", p->pid);
	fd = fopen(statusfile, "r");
	if (fd == NULL) return -1;
	p->uid = -1; // default value
	while (fgets(buffer, sizeof(buffer), fd) != NULL) {
		if (strncmp(buffer, "Uid:", 4) == 0) {
			sscanf(buffer, "Uid:\t%u", &p->uid);
			break;
		}
	}
	fclose(fd);
	
	//read command line
	snprintf(exefile, sizeof(exefile), "/proc/%d/cmdline", p->pid);
	fd = fopen(exefile, "rb");  // Use binary mode to read null-separated args
	if (fd != NULL) {
		size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, fd);
		if (bytes_read > 0) {
			buffer[bytes_read] = '\0';
			// Convert null separators to spaces for easier parsing
			for (size_t i = 0; i < bytes_read; i++) {
				if (buffer[i] == '\0') {
					buffer[i] = ' ';
				}
			}
			// Remove trailing space if exists
			if (bytes_read > 0 && buffer[bytes_read - 1] == ' ') {
				buffer[bytes_read - 1] = '\0';
			}
			strncpy(p->command, buffer, PATH_MAX);
			p->command[PATH_MAX] = '\0';
		} else {
			p->command[0] = '\0';
		}
		fclose(fd);
	} else {
		p->command[0] = '\0';
	}
	return 0;
}

static pid_t getppid_of(pid_t pid)
{
	char statfile[20];
	char buffer[1024];
	snprintf(statfile, sizeof(statfile), "/proc/%d/stat", pid);
	FILE *fd = fopen(statfile, "r");
	if (fd==NULL) return -1;
	if (fgets(buffer, sizeof(buffer), fd)==NULL) {
		fclose(fd);
		return -1;
	}
	fclose(fd);
	char *token = strtok(buffer, " ");
	int i;
	for (i=0; i<3; i++) token = strtok(NULL, " ");
	return atoi(token);
}

static int is_child_of(pid_t child_pid, pid_t parent_pid)
{
	int ppid = child_pid;
	while(ppid > 1 && ppid != parent_pid) {
		ppid = getppid_of(ppid);
	}
	return ppid == parent_pid;
}

int get_next_process(struct process_iterator *it, struct process *p)
{
	if (it->dip == NULL)
	{
		//end of processes
		return -1;
	}
	if (it->filter->pid != 0 && !it->filter->include_children)
	{
		int ret = read_process_info(it->filter->pid, p);
		//p->starttime += it->boot_time;
		closedir(it->dip);
		it->dip = NULL;
		if (ret != 0) return -1;
		
		// Check user filter if enabled
		if (it->filter->filter_by_user == 1 && p->uid != it->filter->uid) {
			return -1;
		}
		// Check if this process should be excluded from limiting
		if (should_exclude_process(p, it->filter->exclude_interactive)) {
			return -1;
		}
		return 0;
	}
	struct dirent *dit = NULL;
	//read in from /proc and seek for process dirs
	while ((dit = readdir(it->dip)) != NULL) {
		if(strtok(dit->d_name, "0123456789") != NULL)
			continue;
		p->pid = atoi(dit->d_name);
		if (it->filter->pid != 0 && it->filter->pid != p->pid && !is_child_of(p->pid, it->filter->pid)) continue;
		if (read_process_info(p->pid, p) != 0) continue;
		
		// Check user filter if enabled
		if (it->filter->filter_by_user == 1 && p->uid != it->filter->uid) {
			continue;
		}
		// Check if this process should be excluded from limiting
		if (should_exclude_process(p, it->filter->exclude_interactive)) {
			continue;
		}
		//p->starttime += it->boot_time;
		break;
	}
	if (dit == NULL)
	{
		//end of processes
		closedir(it->dip);
		it->dip = NULL;
		return -1;
	}
	return 0;
}

int close_process_iterator(struct process_iterator *it) {
	if (it->dip != NULL && closedir(it->dip) == -1) {
		perror("closedir");
		return 1;
	}
	it->dip = NULL;
	
	// Clean up exclusion list when process iterator is closed
	static int cleanup_registered = 0;
	if (!cleanup_registered) {
		atexit(cleanup_exclusion_list);
		cleanup_registered = 1;
	}
	
	return 0;
}
