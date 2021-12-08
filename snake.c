/* ascii-snake: a remake of the old nokia game
 * Copyright (C) 2013 Aleksa Sarai
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* for seeding randomness */
#include <unistd.h>
#include <time.h>

/* for unbuffered input */
#include <termios.h>
#include <fcntl.h>

#if !defined(bool)
#	define true 1
#	define false 0
#	define bool int
#endif

/* OPTIONS (CHANGE THIS BIT) */

#define SNAKE_WRAP		false	/* screen wrapping */
#define SPEED			0.08	/* refresh rate (in seconds) */
#define BONUS_CHANCE	400	/* chance of 1/CHANCE for bonus to appear */

#define BONUS_MIN_TIME	30	/* lower range of lifespan of bonus */
#define BONUS_MAX_TIME	120	/* upper range of lifespan of bonus*/

#define START_SNAKE_LEN	5	/* the beginning length of the snake */

#define SNAKE_BODY		'*'	/* char representing the snake's body */

/* chars representing the snake head, when facing a direction  */
#define SNAKE_HEAD_U	'v'	/* up */
#define SNAKE_HEAD_D	'^'	/* down */
#define SNAKE_HEAD_L	'>'	/* left */
#define SNAKE_HEAD_R	'<'	/* right  */

#define FOOD			'@'	/* char representing food */
#define BONUS			'$'	/* char representing */

#define FOOD_SCORE		1	/* score increase when snake eats food */
#define BONUS_SCORE		10	/* score increase when snake eats bonus */

#define SCREEN_WIDTH	30	/* the virtual screen width */
#define SCREEN_HEIGHT	20	/* the virtual screen height */

#define BORDER_CORNER	'+'	/* character at corners of border */
#define BORDER_VERT		'|'	/* character for vertical border */
#define BORDER_HORI		'-'	/* character for horizontal border */

/* END OPTIONS (the rest is more dev stuff ) */

#define ANSI_RED		"\x1b[1;31m"
#define ANSI_GREEN		"\x1b[1;32m"
#define ANSI_YELLOW		"\x1b[1;33m"
#define ANSI_BLUE		"\x1b[1;34m"
#define ANSI_WHITE		"\x1b[1;37m"
#define ANSI_CLEAR		"\x1b[0m"

#define SCORE_FORMAT	"Score: " ANSI_BLUE   "%d" ANSI_CLEAR
#define TIMER_FORMAT	"Timer: " ANSI_YELLOW "%d" ANSI_CLEAR

/* for positions, (x, y) */
#define X 0
#define Y 1

#define IS_SNAKE(ch) (ch == SNAKE_BODY || ch == SNAKE_HEAD_U || ch == SNAKE_HEAD_D || ch == SNAKE_HEAD_L || ch == SNAKE_HEAD_R)

int i, x, y;

/* global states */
enum {
	win,
	loss
};

int quit = false;
char **game_state = NULL; /* stores the string of the current game state */
char *border = NULL;

/* snake */
int snake_len;
int snake_head[2]; /* position of head */
int **snake_body = NULL; /* position of all body parts */

enum {
	up,
	down,
	left,
	right
};

int snake_direction = right;

/* food / score */
int score;
int food[2]; /* (x, y) of current food position */
int bonus[2]; /* (x, y) of current bonus position */
int timer;

char getch(void) {
	static struct termios old, new;

	/* get old settings */
	tcgetattr(0, &old);
	new = old;

	/* disable buffered i/o and echo */
	new.c_lflag &= ~ICANON;
	new.c_lflag &= ~ECHO;

	/* set new terminal settings */
	tcsetattr(0, TCSANOW, &new);

	/* get char and reset terminal settings */
	char ch = getchar();
	tcsetattr(0, TCSANOW, &old);

	/* if ctrl-c was pressed, exit */
	if(ch == 3)
		exit(2);

	return ch;
} /* getch_() */

int snake_rand(int min, int max) {
	/* generate x, where min <= x <= max */
	return (rand() % (max + 1 - min)) + min;
} /* snake_rand() */

void seed_rand() {
	srand(time(NULL) ^ getpid());
}

void border_init() {
	border = calloc(SCREEN_WIDTH + 3, 1);

	for(i = 0; i < SCREEN_WIDTH; i++)
		border[i + 1] = BORDER_HORI;

	border[0] = border[SCREEN_WIDTH + 1] = BORDER_CORNER;
} /* border_init() */

void default_score() {
	score = 0;
}

void snake_head_init() {
	snake_len = START_SNAKE_LEN;
	snake_head[X] = SCREEN_WIDTH / 2;
	snake_head[Y] = SCREEN_HEIGHT / 2;
}

void snake_body_init() {
	snake_body = malloc(snake_len * sizeof(int *));

	for(i = 0; i < snake_len; i++) {
		snake_body[i] = malloc(2 * sizeof(int));

		/* set default position */
		snake_body[i][X] = snake_head[X] - i;
		snake_body[i][Y] = snake_head[Y];
	}
}

void snake_init() {
	snake_head_init();
	snake_body_init();
}

void food_init() {
	food[X] = snake_rand(0, SCREEN_WIDTH - 1);
	food[Y] = snake_rand(0, SCREEN_HEIGHT - 1);
}

void bonus_init() {
	bonus[X] = -1;
	bonus[Y] = -1;
	timer = -1;
}

void state_init() {
	game_state = calloc(SCREEN_HEIGHT * SCREEN_WIDTH, sizeof(char *));
	for(i = 0; i < SCREEN_WIDTH; i++) {
		game_state[i] = calloc(SCREEN_WIDTH + 1, 1);
		game_state[i][SCREEN_WIDTH] = '\0';
	}
}

void init(void) {
	seed_rand();
	default_score();
	snake_init();
	food_init();
	bonus_init();
	state_init();
	border_init();
} /* init() */

void mup() { if(snake_direction != down) snake_direction = up; }
void mdown() { if(snake_direction != up) snake_direction = down; }
void mleft() { if(snake_direction != right) snake_direction = left; }
void mright() { if(snake_direction != left) snake_direction = right; }

void choose_direction(int ch) {
	/* 'normal' input switch */
	int weird = false;
	switch(ch) {
		case 'q':
		case 'Q':
			quit = true;
			break;
		case 'w':
		case 'W':
			mup();
			break;
		case 's':
		case 'S':
			mdown();
			break;
		case 'd':
		case 'D':
			mright();
			break;
		case 'a':
		case 'A':
			mleft();
			break;
		case 27:
			weird = true;
			break;
	}

	/* check if key was 'normal' */
	if(!weird)
		return;

	/* up, down, left, right keys -- specific to my machine and os */
	if(ch == 27 && getch() == 91) {
		switch(getch()) {
			case 65:
				mup();
				break;
			case 66:
				mdown();
				break;
			case 67:
				mright();
				break;
			case 68:
				mleft();
				break;
			default:
				break;
		}
	}
}

int non_blocking_input() {
	/* get non-blocking input */
	int old = fcntl(0, F_GETFL);
	fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

	int ch = getch();

	fcntl(0, F_SETFL, old);

	return ch;
}

void snake_input(void) {
	choose_direction(non_blocking_input());
} /* snake_input() */

/* GAME STATE:
 *   Y ^
 *   0 |       @
 *     |       x
 *     |  ******
 *     |  *
 * max +------------>
 *     0        max X
 *
 * BODY:
 * [head, ..., tail]
 */

void shift_snake(void) {
	/* shifts all snake body parts down */
	int i;
	for(i = snake_len - 1; i > 0; i--) {
		snake_body[i][X] = snake_body[i-1][X];
		snake_body[i][Y] = snake_body[i-1][Y];
	}

	/* set top of head */
	snake_body[0][X] = snake_head[X];
	snake_body[0][Y] = snake_head[Y];
} /* shift_snake() */

int in_snake(int x, int y) {
	if(snake_head[X] == x && snake_head[Y] == y)
		return true;

	int i;
	for(i = 0; i < snake_len; i++) {
		if(snake_body[i][X] == x && snake_body[i][Y] == y)
			return true;
	}

	return false;
} /* in_snake() */

void snake_head_update() {
	switch(snake_direction) {
		case up:
			snake_head[Y]--;
			break;
		case down:
			snake_head[Y]++;
			break;
		case left:
			snake_head[X]--;
			break;
		case right:
			snake_head[X]++;
			break;
	}
}

bool snake_intersects() {
	for(i = 1; i < snake_len; i++) {
		if(snake_head[X] == snake_body[i][X] && snake_head[Y] == snake_body[i][Y]) {
			quit = true;
			return true;
		}
	}
	 return false;
}

bool snake_is_outside_screen() {
	/* check if snake is outside screen */
	if(snake_head[Y] < 0 || snake_head[Y] >= SCREEN_HEIGHT ||
	   snake_head[X] < 0 || snake_head[X] >= SCREEN_WIDTH) {
		if(SNAKE_WRAP) {
			/* if wrapping is enabled, wrap the screen */
			snake_head[X] = (snake_head[X] < 0) ?  SCREEN_WIDTH - 1 : snake_head[X];
			snake_head[Y] = (snake_head[Y] < 0) ? SCREEN_HEIGHT - 1 : snake_head[Y];

			snake_head[X] %= SCREEN_WIDTH;
			snake_head[Y] %= SCREEN_HEIGHT;
		} else {
			/* otherwise, game over */
			quit = true;
			return true;
		}
	}
	return false;
}

void eat_food() {
	if(snake_head[X] == food[X] && snake_head[Y] == food[Y]) {
		/* update score */
		score += FOOD_SCORE;

		/* reallocate body */
		snake_body = realloc(snake_body, (snake_len + 1) * sizeof(int *));
		snake_body[snake_len] = malloc(2 * sizeof(int));

		/* set body to defaults */
		snake_body[snake_len][X] = -1;
		snake_body[snake_len][Y] = -1;

		/* update length of snake */
		snake_len++;

		/* regenerate food */
		do {
			food[X] = snake_rand(0, SCREEN_WIDTH - 1);
			food[Y] = snake_rand(0, SCREEN_HEIGHT - 1);
		} while(in_snake(food[X], food[Y]) || (food[X] == bonus[X] && food[Y] == bonus[Y]));
	}
}

void eat_bonus() {
	if(snake_head[X] == bonus[X] && snake_head[Y] == bonus[Y]) {
		/* update score */
		score += BONUS_SCORE;

		/* delete bonus */
		bonus[X] = -1;
		bonus[Y] = -1;
		timer = -1;
	}
}

void update_bonus() {
	/* updating bonus */
	if(bonus[X] >= 0 && bonus[Y] >= 0) {
		/* decrement bonus timer */
		timer--;

		/* if time has run out, remove bonus food */
		if(!timer) {
			bonus[X] = -1;
			bonus[Y] = -1;
			timer = -1;
		}
	} else {
		/* no bonus -- randomly generate */
		if(snake_rand(0, BONUS_CHANCE) == BONUS_CHANCE / 2) {
			/* add bonus */
			do {
				bonus[X] = snake_rand(0, SCREEN_WIDTH - 1);
				bonus[Y] = snake_rand(0, SCREEN_HEIGHT - 1);
			} while(in_snake(bonus[X], bonus[Y]) || (bonus[X] == food[X] && bonus[Y] == food[Y]));

			/* set timer */
			timer = snake_rand(BONUS_MIN_TIME, BONUS_MAX_TIME);
		}
	}
}

void clear_screen() {
	for(y = 0; y < SCREEN_HEIGHT; y++) {
		for(x = 0; x < SCREEN_WIDTH; x++) {
			game_state[y][x] = ' ';
		}
	}
}

void draw_snake() {
	for(i = 1; i < snake_len; i++) {
		x = snake_body[i][X];
		y = snake_body[i][Y];

		/* ignore 'fake' positions */
		if(x < 0 || y < 0)
			continue;

		game_state[y][x] = SNAKE_BODY;
	}
}

void draw_food() {
	x = food[X];
	y = food[Y];
	if(x >= 0 && y >= 0)
		game_state[y][x] = FOOD;
}

void draw_bonus() {
	x = bonus[X];
	y = bonus[Y];
	if(x >= 0 && y >= 0)
		game_state[y][x] = BONUS;
}

void draw_snake_head() {
	/* add snake head */
	x = snake_head[X];
	y = snake_head[Y];
	if(x >= 0 && y >= 0) {
		char head = 'X';
		switch(snake_direction) {
			case up:
				head = SNAKE_HEAD_U;
				break;
			case down:
				head = SNAKE_HEAD_D;
				break;
			case left:
				head = SNAKE_HEAD_L;
				break;
			case right:
				head = SNAKE_HEAD_R;
				break;
		}
		game_state[y][x] = head;
	}
}

void snake_update(void) {
	snake_head_update();
	if (snake_intersects()) return;
	if (snake_is_outside_screen()) return;
	eat_food();
	eat_bonus();
	update_bonus();
	shift_snake();

	clear_screen();
	draw_snake();
	draw_food();
	draw_bonus();
	draw_snake_head();
} /* snake_update() */

void clr_line(void) {
	printf("\x1b[s"); /* save current cursor position */
	printf("%*s", SCREEN_WIDTH, ""); /* blank out virual screen */
	printf("\x1b[u"); /* move cursor back to saved position */

	fflush(stdout);
} /* clr_line() */

void print_game_state() {
	for(i = 0; i < SCREEN_HEIGHT; i++)
		printf("%c%s%c\n", BORDER_VERT, game_state[i], BORDER_VERT);
}

void print_score() {
	clr_line();
	printf("\n" SCORE_FORMAT "\n", score);
}

void print_timer() {
	if(timer >= 0) {
		clr_line();
		printf(TIMER_FORMAT "\n", timer);
	}
}

void print_border() {
	printf("%s\n", border);
}

void move_cursor(int row, int column) {
	printf("\x1b[%d;%df", row, column);
	fflush(stdout);
} /* snake_redraw() */


void snake_redraw(void) {
	move_cursor(0, 0);
	print_border();
	print_game_state();
	print_border();
	print_score();
	print_timer();
	clr_line();
} /* snake_redraw() */

int snake_end_state(void) {
	for(y = 0; y < SCREEN_HEIGHT; y++)
		for(x = 0; x < SCREEN_WIDTH; x++)
			if(!IS_SNAKE(game_state[y][x]))
				return loss;

	return win;
} /* snake_end_state() */

void clr_screen() {
	printf("\x1b[H\x1b[2J");
	fflush(stdout);
}

void main_loop() {
	while(!quit) {
		snake_input();  /* get input */
		snake_update(); /* update game state */
		snake_redraw(); /* redraw screen */

		/* wait SPEED seconds */
		usleep(SPEED * 1000000L);
	}
}

void end_game() {
	char *msg = NULL;
	int end_state = snake_end_state();

	switch(end_state) {
		case win:
			msg = "You Won!";
			break;
		case loss:
		default:
			msg = "Game Over!";
			break;
	}

	move_cursor((SCREEN_HEIGHT / 2) + 2, (SCREEN_WIDTH / 2) - (strlen(msg) / 2) + 2);
	printf("%s%s%s", ANSI_RED, msg, ANSI_CLEAR);
	fflush(stdout);
}

void free_memory() {
	for(i = 0; i < SCREEN_HEIGHT * SCREEN_WIDTH; i++)
		free(game_state[i]);
	free(game_state);

	for(i = 0; i < snake_len; i++)
		free(snake_body[i]);
	free(snake_body);
	free(border);
}

int main(void) {
	clr_screen();

	/* intialise snake */
	init();
	snake_redraw();

	main_loop();
	end_game();

	move_cursor(SCREEN_HEIGHT + 5 + (timer >= 0), 0); /* move to default, screen_height + borders + spaces for score, etc. */
	return 0;
}
