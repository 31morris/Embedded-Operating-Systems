#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

// 定義菜單結構
typedef struct {
    char name[50];
    int price;
} MenuItem;

// 定義餐廳結構
typedef struct {
    char name[50];
    char distance[10];
    MenuItem items[2];  // 每家餐廳有兩個菜單項
} Restaurant;

// 計算數字的位數
int countDigits(int num) {
    int count = 0;
    while (num != 0) {
        num /= 10;
        ++count;
    }
    return count;
}
int main() {
    
    int menuOption = 0;
    int orderConfirmed = 0;
    int quantity = 0;

    //定義三家餐廳
    Restaurant restaurants[3];
    strcpy(restaurants[0].name, "Dessert shop");
    strcpy(restaurants[0].distance, "3km");
    strcpy(restaurants[1].name, "Beverage shop");
    strcpy(restaurants[1].distance, "5km");
    strcpy(restaurants[2].name, "Diner");
    strcpy(restaurants[2].distance, "8km");

    // 菜單項
    strcpy(restaurants[0].items[0].name, "cookie");
    restaurants[0].items[0].price = 60;
    strcpy(restaurants[0].items[1].name, "cake");
    restaurants[0].items[1].price = 80;

    strcpy(restaurants[1].items[0].name, "tea");
    restaurants[1].items[0].price = 40;
    strcpy(restaurants[1].items[1].name, "boba");
    restaurants[1].items[1].price = 70;

    strcpy(restaurants[2].items[0].name, "fried rice");
    restaurants[2].items[0].price = 120;
    strcpy(restaurants[2].items[1].name, "egg-drop soup");
    restaurants[2].items[1].price = 50;

    // 開啟設備檔案
    int fd1, fd2;
    fd1 = open("/dev/distance_device", O_RDWR); //led
    fd2 = open("/dev/money_device", O_RDWR);   //7-seg
    
    if(fd1 < 0 || fd2 < 0){
        printf("open device error\n");
        return 0;
    }

    while (1) {
        if (menuOption == 0) {
            printf("1. shop list\n");
            printf("2. order\n");
            scanf("%d", &menuOption);
        }
        else if (menuOption == 1) {
            // 顯示餐廳列表
            for (int i = 0; i < 3; i++) {
                printf("%s (%s)\n", restaurants[i].name, restaurants[i].distance);
            }
            //按任意鍵返回主選單
            menuOption = 0;
            getchar();
            getchar();
        }
        else if (menuOption == 2) {
            // 開始訂餐
            printf("Please choose from 1~3\n");
            for (int i = 0; i < 3; i++) {
                printf("%d. %s\n", i + 1, restaurants[i].name);
            }
            //輸入選項
            int selectedRestaurant;
            scanf("%d", &selectedRestaurant);
            if (selectedRestaurant < 1 || selectedRestaurant > 3) {
                printf("無效選項，請重新輸入\n");
                continue;
            }
            //選餐點，確認或取消
            int selectedMenuItem;
            // 計算總金額
            int totalAmount = 0;

            while (1) {
                // 選擇餐點
                /*Please choose from 1~4  
                1. cookie: $60
                 2. cake: $80
                 3. confirm
                 4. cancel  */                  
                printf(" Please choose from 1~4\n");
                printf("1. %s: $%d\n", restaurants[selectedRestaurant - 1].items[0].name, restaurants[selectedRestaurant - 1].items[0].price);
                printf("2. %s: $%d\n", restaurants[selectedRestaurant - 1].items[1].name, restaurants[selectedRestaurant - 1].items[1].price);
                printf("3. confirm\n");
                printf("4. cancel\n");
                // printf("請輸入選項：");
                scanf("%d", &selectedMenuItem);

                if (selectedMenuItem == 3) {
                    // 送出訂單
                    if (totalAmount == 0) {
                        printf("請先選擇餐點\n");
                        continue;
                    }

                    // printf("訂單已從%s送出，總金額：%d\n", restaurants[selectedRestaurant - 1].name, totalAmount);
                    printf("Please wait for a few minutes...\n");

                    //change the total amount by int to char
                    char totalAmount_char[countDigits(totalAmount)];
                    sprintf(totalAmount_char, "%d", totalAmount);

                    // write the distance and money to my custom device
                    write(fd1, restaurants[selectedRestaurant - 1].distance, strlen(restaurants[selectedRestaurant - 1].distance));
                    printf("Please pick up your meal!\n");
                    write(fd2, totalAmount_char, strlen(totalAmount_char));
                    // 等待距離時間
                    // int distance = restaurants[selectedRestaurant - 1].distance[0] - '0'; // 把距離字串第一個字元轉成整數
                    // sleep(distance-2); // 等距離秒數
                    

                    orderConfirmed = 1;
                    break;
                }
                else if (selectedMenuItem == 4) {
                    // 取消點餐
                    break;
                }
                else if (selectedMenuItem == 1 || selectedMenuItem == 2) {
                    // 點餐點
                    int menuItemIndex = selectedMenuItem - 1;
                    printf("How many?");
                    int itemQuantity;
                    scanf("%d", &itemQuantity);
                    totalAmount += (restaurants[selectedRestaurant - 1].items[menuItemIndex].price * itemQuantity);
                }
                else {
                    printf("無效選項，請重新輸入\n");
                }
            }

            if (orderConfirmed==1) {
                getchar(); // 等待使用者按任意鍵
                getchar();
                menuOption = 0; 
                orderConfirmed = 0; 
            }
            // 如果點餐被取消，返回主選單
            if (orderConfirmed==0) {
                menuOption = 0;
            }
        }
        else {
            printf("無效選項，請重新輸入\n");
            menuOption = 0;
        }
    }

    return 0;
}