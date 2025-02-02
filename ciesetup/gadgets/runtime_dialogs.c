/*
 * ciesetup - The ultimate workarounds to fix an ancient game
 * Written starting in 2022 by contributors (see CREDITS.txt)
 * To the extent possible under law, the author(s) have dedicated all copyright and related and neighboring rights to this software to the public domain worldwide. This software is distributed without any warranty.
 * You should have received a copy of the CC0 Public Domain Dedication along with this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

// This is where the fun begins
// Also note we deliberately avoid doing anything that could poison our symbols with libc versioning

// Definitions

extern int puts(const char * str);
extern int putchar(int c);
extern int printf(const char * fmt, ...);
extern int scanf(const char * fmt, ...);

// C++ structs

typedef struct {
	int length;
	int bufferSize;
	int idk1;
	void * idk2;
	char content[];
} cppstring_content_t;

typedef struct {
	void * content_fwd;
} cppstring_t;

// Error dialog replacement (1)

typedef struct {
	cppstring_t error;
	char moveCameraSet;
	char moveCameraAllowed;
} runtime_error_dialog_t;

static void putcs(cppstring_t * cpp) {
	cppstring_content_t * ca = cpp->content_fwd - 16;
	for (int i = 0; i < ca->length; i++)
		putchar(ca->content[i]);
}

int DisplayDialog__18RuntimeErrorDialog(runtime_error_dialog_t * dialog) {
	puts("CAOS Error: ");
	putcs(&dialog->error);
	printf("Enter 1 to ignore the error, 2 to freeze the agent, 3 to kill the agent, 4 to pause the game, or 5 to stop the script:\n");
	int retCode = 1;
	scanf("%i", &retCode);
	printf("Ok, %i\n", retCode);
	dialog->moveCameraSet = 1;
	return retCode;
}

// Error dialog replacement (2)

typedef struct {
	cppstring_t error;
	cppstring_t source;
} error_dialog_t;

int DisplayDialog__11ErrorDialog(error_dialog_t * dialog) {
	printf("System Error: ");
	putcs(&dialog->error);
	printf("\nSOURCE: ");
	putcs(&dialog->source);
	printf("\n");
	printf("Enter 0 to continue, 1 to quit, and 2 to abort:\n");
	int retCode = 0;
	scanf("%i", &retCode);
	printf("Ok, %i\n", retCode);
	return retCode;
}

void gtk_init() {
}
void gtk_widget_destroy() {
}

