
SESSION="camstream"

# 重新啟動 session（如已存在）
tmux has-session -t $SESSION 2>/dev/null
if [ $? -eq 0 ]; then
    tmux kill-session -t $SESSION
fi

# 建立新 session，視窗 0 啟動 /dev/video1 → port 5000
tmux new-session -d -s $SESSION "./udp_send1 /dev/video0 5000"

# 垂直切出第二個窗格 → 啟動 /dev/video2 → port 5001
tmux split-window -h
tmux send-keys "./udp_send1 /dev/video2 5001" C-m

# 設定顯示 layout，並 attach
tmux select-layout even-horizontal
tmux attach-session -t $SESSION