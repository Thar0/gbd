#ifndef ANSI_COLOR_CODES_H_
#define ANSI_COLOR_CODES_H_

// based on https://gist.github.com/RabaDabaDoba/145049536f815903c79944599c6f952a

//Regular text
#define CLR_BLACK "\e[0;30m"
#define CLR_RED "\e[0;31m"
#define CLR_GREEN "\e[0;32m"
#define CLR_YELLOW "\e[0;33m"
#define CLR_BLUE "\e[0;34m"
#define CLR_PURPLE "\e[0;35m"
#define CLR_CYAN "\e[0;36m"
#define CLR_WHITE "\e[0;37m"

//Regular bold text
#define CLR_BLACK_B "\e[1;30m"
#define CLR_RED_B "\e[1;31m"
#define CLR_GREEN_B "\e[1;32m"
#define CLR_YELLOW_B "\e[1;33m"
#define CLR_BLUE_B "\e[1;34m"
#define CLR_PURPLE_B "\e[1;35m"
#define CLR_CYAN_B "\e[1;36m"
#define CLR_WHITE_B "\e[1;37m"

//Regular underline text
#define CLR_BLACK_U "\e[4;30m"
#define CLR_RED_U "\e[4;31m"
#define CLR_GREEN_U "\e[4;32m"
#define CLR_YELLOW_U "\e[4;33m"
#define CLR_BLUE_U "\e[4;34m"
#define CLR_PURPLE_U "\e[4;35m"
#define CLR_CYAN_U "\e[4;36m"
#define CLR_WHITE_U "\e[4;37m"

//Regular background
#define CLR_BG_BLACK "\e[40m"
#define CLR_BG_RED "\e[41m"
#define CLR_BG_GREEN "\e[42m"
#define CLR_BG_YELLOW "\e[43m"
#define CLR_BG_BLUE "\e[44m"
#define CLR_BG_PURPLE "\e[45m"
#define CLR_BG_CYAN "\e[46m"
#define CLR_BG_WHITE "\e[47m"

//High intensty background
#define CLR_BG_BLACK_H "\e[0;100m"
#define CLR_BG_RED_H "\e[0;101m"
#define CLR_BG_GREEN_H "\e[0;102m"
#define CLR_BG_YELLOW_H "\e[0;103m"
#define CLR_BG_BLUE_H "\e[0;104m"
#define CLR_BG_PURPLE_H "\e[0;105m"
#define CLR_BG_CYAN_H "\e[0;106m"
#define CLR_BG_WHITE_H "\e[0;107m"

//High intensty text
#define CLR_BLACK_H "\e[0;90m"
#define CLR_RED_H "\e[0;91m"
#define CLR_GREEN_H "\e[0;92m"
#define CLR_YELLOW_H "\e[0;93m"
#define CLR_BLUE_H "\e[0;94m"
#define CLR_PURPLE_H "\e[0;95m"
#define CLR_CYAN_H "\e[0;96m"
#define CLR_WHITE_H "\e[0;97m"

//Bold high intensity text
#define CLR_BLACK_BH "\e[1;90m"
#define CLR_RED_BH "\e[1;91m"
#define CLR_GREEN_BH "\e[1;92m"
#define CLR_YELLOW_BH "\e[1;93m"
#define CLR_BLUE_BH "\e[1;94m"
#define CLR_PURPLE_BH "\e[1;95m"
#define CLR_CYAN_BH "\e[1;96m"
#define CLR_WHITE_BH "\e[1;97m"

//Reset
#define CLR_RESET "\e[0m"

#endif
