#include <stdio.h>
#include <windows.h>
#include <unistd.h>
#include "menu.h"
#include "common.h"
#include "player.h"
//Creating Menu (GUI) for Engine

void showMenu(){
   int choice;
   Player p1;
   system("color F1");
   system("clear");
   printf("---------------------------------------------------\n");
   printf("-        MatchMaking Game Engine Simulator        -\n");
   printf("---------------------------------------------------\n");
   printf("Players:\nWaiting:\nActive Players:\nServers:\n");
   printf("---------------------------------------------------\n");
   printf("1.Register Player\n2.Remove Player\n3.Search Player\n4.View Player Profile\n5.Join matchmaking\n6.Leave matchmaking\n7.Run matchmaking\n8.View Active matches\n9.Complete matches\n10.View Server\n11.Find best server\n12.View Statistics\n13.Run Simulation\n0.Exit\n");
   printf("Enter a choice:");
   scanf("%d",&choice);
   switch(choice){
      case 0:
         return;
      case 1:
        playerInit(&p1,101,"Ghast",1500,30,REG_IND);
        playerPrint(&p1);
         break;
      case 2:

         break;
      case 3:

         break;
      case 4:

         break;
      case 5:

         break;
      case 6:

         break;
      case 7:

         break;
      case 8:

         break;
      case 9:

         break;
      case 10
         :
         break;
      case 11
         :
         break;
      case 12
         :
         break;
      case 13
         :
         break;

   }
}