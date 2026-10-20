cmake_minimum_required(VERSION 3.11)
project(CircleWars)

# C標準を指定（raylibや数学関数を安定させるため）
set(CMAKE_C_STANDARD 99)

# 1. ローカルのRaylib（lib/raylibディレクトリ）をビルド対象に追加
add_subdirectory(lib/raylib EXCLUDE_FROM_ALL)

# 2. 実行ファイルの作成
add_executable(circle_wars circle_wars.c)

# 3. インクルードパスの設定
target_include_directories(circle_wars PUBLIC lib/raylib/src)

# 4. ライブラリをリンクする
target_link_libraries(circle_wars raylib m)

if (UNIX AND NOT APPLE)
    # WSLg/Linux環境で必要な依存ライブラリ
    # raylibが内部で使用する X11 やオーディオ関連(asound, pulse)をリンク
    target_link_libraries(circle_wars 
        pthread 
        dl 
        rt 
        X11 
        asound 
        pulse 
        pulse-simple
    )
endif()