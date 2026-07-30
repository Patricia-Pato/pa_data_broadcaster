#!/usr/bin/env bash
#
# RTT ログ取得スクリプト
#
# JLinkExe がターゲット接続中に開く RTT telnet サーバ (port 19021) へ
# nc で接続し、RTT Ch0 のログを標準出力へ流す。
# デフォルトでリセットを行うため、起動ログを先頭から取得できる。
#
# 使い方:
#   ./scripts/rtt_log.sh                 # リセットして 10 秒間ログ取得
#   ./scripts/rtt_log.sh 30              # リセットして 30 秒間ログ取得
#   ./scripts/rtt_log.sh 30 --no-reset   # リセットせずに取得 (バックログ含む)
#   SN=xxxxxxxxx ./scripts/rtt_log.sh    # シリアル番号指定 (2台接続時)
#
set -u

DURATION="${1:-10}"
RESET_OPT="${2:-}"
SN="${SN:-1050051293}"
RTT_PORT=19021

if [ "$RESET_OPT" = "--no-reset" ]; then
	JLINK_CMDS="g"
else
	JLINK_CMDS="r\ng"
fi

pkill -9 -f JLinkExe 2>/dev/null
sleep 1

# JLinkExe を接続維持したままバックグラウンドで起動
(
	printf "${JLINK_CMDS}\n"
	sleep "$((DURATION + 5))"
	printf 'q\n'
) | JLinkExe -Device NRF5340_XXAA_APP -If SWD -Speed 4000 -AutoConnect 1 \
	-SelectEmuBySN "$SN" > /dev/null 2>&1 &
JLINK_PID=$!

trap 'kill $JLINK_PID 2>/dev/null' EXIT

sleep 4

# RTT telnet からログを取得 (ヘッダ 3 行は SEGGER の接続メッセージ)
timeout "$DURATION" nc localhost "$RTT_PORT" || true
