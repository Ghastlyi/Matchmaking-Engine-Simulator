#include <stdio.h>
#include <windows.h>
#include <unistd.h>
#include "menu.h"
#include "common.h"
#include "player.h"
#include "hashmap.h"
//Creating Menu (GUI) for Engine

void showMenu(PlayerDatabase *db){
   int choice;
   system("color F1");
   while(1){
     printf("---------------------------------------------------\n");
     printf("-        MatchMaking Game Engine Simulator        -\n");
     printf("---------------------------------------------------\n");
     printf("Players:\nWaiting:\nActive Players:\nServers:\n");
     printf("---------------------------------------------------\n");
     printf("1.Register Player\n2.Remove Player\n3.Search Player\n4.View Player Profile\n5.Join matchmaking\n6.Leave matchmaking\n7.Run matchmaking\n8.View Active matches\n9.Complete matches\n10.View Server\n11.Find best server\n12.View Statistics\n13.Run Simulation\n14.Empty database\n15.Print database\n0.Exit\n");
     printf("Enter a choice:");
     scanf("%d",&choice);
     switch(choice){
        case 0:
        return;
        case 1:
            Player p=registerPlayer();
            player_dbadd(db,p);
        break;
        case 2:{
           int id;
           printf("\nEnter player ID to remove: ");
           scanf("%d", &id);
           player_dbremove(db, id);
           break;
         }
           case 3:{
              int id;
              printf("\nEnter player ID:");
              scanf("%d", &id);
              HashNode *node = hashmap_search(&db->index, id);
              if (node == NULL){
                 printf("Player not found.\n");
               }
               else{
                  Player *player = player_dbget(db, node->playerIndex);
                  printf("\nPlayer found!\n");
                  playerPrint(player);
               }
            }
        break;
        case 4:{
           int id;
           printf("\nEnter player ID: ");
           scanf("%d", &id);
           HashNode *node = hashmap_search(&db->index, id);
           if (node == NULL){
              printf("Player not found.\n");
            }
            else{
               Player *player = player_dbget(db, node->playerIndex);
               profileprint(player);
            }
            break;
         }
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
        case 10:
        break;
        case 11:
        break;
        case 12:
        break;
        case 13:
        break;
        case 14:
         player_dbclear(db);
         printf("Database emptied.\n");
           break;
        case 15:
        player_dbPrint(db);
        break;
      }
   }
}