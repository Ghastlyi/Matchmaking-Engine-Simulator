#include <stdio.h>
#include <windows.h>
#include <stdlib.h>
#include <unistd.h>
#include "menu.h"
int main(){
   PlayerDatabase db;
   player_dbinit(&db);
   showMenu(&db);
   player_dbdestroy(&db);
   return 0;
}