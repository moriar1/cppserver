# CPPServer

独自のスレッドプールとBSDソケット使用したHTTPサーバ。

## ソースコードからのビルド

**前提条件:**

- Unix系OS: Linux, FreeBSD
- C++ コンパイラ: GCCまたはClang
- CMake
- Ninja (任意)

Ubuntuでの依存関係のインストール:

```sh
sudo -y apt install build-essential cmake ninja-build
```

**リリースビルド**:

```sh
cmake -DCMAKE_BUILD_TYPE=Release -B build -S .
cmake --build build
```

**Debug こんぱいる** (Sanitizer、 Ninja、 Clangを使用):

```sh
CC=clang CXX=clang++ cmake -DCMAKE_BUILD_TYPE=Debug -B build -S . -G Ninja -DUSE_SANITIZER=address,undefined
cmake --build build
```

## 使い方

```sh
./build/cppserver
```

**出力例**:

```text
[INFO] main.cpp:60: binding to ::
[INFO] main.cpp:124: waiting connections...
[INFO] main.cpp:111: got connection from ::1
[INFO] httphandler.cpp:116: client ::1 status: 200, closing connection...
[INFO] main.cpp:111: got connection from ::1
[INFO] httphandler.cpp:116: client ::1 status: 404, closing connection...
```

*終了するにはCtrl+Cを押します。*

## テスト

### ウェブブラウザでのかくにん

`cppserver`を起動後、ブラウザでつぎのリンクを開きます: `http://localhost:3490`

### curlで確認

```sh
# HEAD リクエスト:
curl --head localhost:3490

# GET リクエスト:
curl localhost:3490
```
