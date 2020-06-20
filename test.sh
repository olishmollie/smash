#!/usr/bin/env sh

set -e

echo "Hello World | Life is Good > Great $"
echo 'Hello World | Life is Good > Great $'

sleep 1

ls

sleep 1

ls -l -a

sleep 1

ls -la

sleep 1

ps -aux

sleep 1

echo "Sleeping for 5 seconds..."
sleep 5

sleep 1

ps aux>a

sleep 1

grep /init < a

sleep 1

grep /init < a > b

sleep 1

echo hello | wc -c

sleep 1

ps aux | awk '/init/{print $1}' | sort -r

sleep 1

ps aux | awk '/init/{print $1}' | sort -r | awk '/ro/' | grep ro

sleep 1

ps aux | awk '{print $1$11}' | sort -r | grep root

sleep 1

ps aux > test.txt
awk '{print $1$11}'<test.txt | head -10 | head -8 | head -7 | sort > output.txt
cat output.txt

sleep 1

sleep 1 &
sleep 2 &
sleep 20 &
jobs

sleep 1
