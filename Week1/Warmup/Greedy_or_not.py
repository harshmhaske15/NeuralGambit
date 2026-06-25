n = int(input())
arr = list(map(int, input().split()))
dp = [[0] * n for _ in range(n)]

for i in range(n):
    dp[i][i] = arr[i]

for length in range(2, n + 1):
    for i in range(n - length + 1):
        j = i + length - 1
        take_left = arr[i] - dp[i + 1][j]
        take_right = arr[j] - dp[i][j - 1]
        dp[i][j] = max(take_left, take_right)

result = dp[0][n - 1]

if result > 0:
    print("Player 1 wins")
elif result < 0:
    print("Player 2 wins")
else:
    print("Draw")
