"""PlatformIO pre-build script: 個人設定 (.env) をビルド定義へ流し込む。

リポジトリには個人固有の値(自前 OTA 配信先の IP など)を**入れない**方針。
各自がリポジトリ直下に `.env` を作り(テンプレートは `.env.template`)、
そこに書いた値を -D マクロとして焼き込む。`.env` は .gitignore 済み。

優先順位: 環境変数 > .env > C 側の既定値 (OtaUpdater.cpp の #ifndef)

    X3DP_OTA_LATEST_URL   安定版の問い合わせ先 (latest.json / GitHub API)
    X3DP_OTA_LIST_URL     RC/Dev チャネル用のリリース一覧
    X3DP_VERSION          ファームの版表記。未設定なら scripts/git_branch.py が git から作る

git_branch.py より先に走らせること(version を pinned として認識させるため)。
"""

import os
import pathlib
import sys

Import("env")  # noqa: F821  (PlatformIO が注入する)

DEFAULTS_NOTE = "未設定なら C 側の既定値(公開リポジトリの GitHub API)を使います"


def warn(msg):
    print(f"WARNING [x3dp_env.py]: {msg}", file=sys.stderr)


def load_dotenv(path):
    """KEY=VALUE 形式だけを読む(export 接頭辞・引用符・# コメント対応)。"""
    values = {}
    if not path.exists():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export "):].strip()
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        if key:
            values[key] = value
    return values


def pick(name, dotenv):
    value = os.environ.get(name)
    if value is not None and value != "":
        return value, "env"
    value = dotenv.get(name)
    if value is not None and value != "":
        return value, ".env"
    return None, None


def c_string_define(name, value):
    """-DNAME=\\"value\\" 相当。引用符とバックスラッシュは落とす。"""
    cleaned = str(value).replace("\\", "").replace('"', "")
    return (name, f'\\"{cleaned}\\"')


dotenv = load_dotenv(pathlib.Path(env.subst("$PROJECT_DIR")) / ".env")  # noqa: F821
defines = []

for key in ("X3DP_OTA_LATEST_URL", "X3DP_OTA_LIST_URL"):
    value, origin = pick(key, dotenv)
    if value is None:
        print(f"X3DP: {key} は{DEFAULTS_NOTE}")
        continue
    if not (value.startswith("http://") or value.startswith("https://")):
        warn(f"{key}={value!r} は http(s) URL ではないので無視します")
        continue
    defines.append(c_string_define(key, value))
    print(f"X3DP: {key} <- {origin} ({value})")

version, origin = pick("X3DP_VERSION", dotenv)
if version is not None:
    defines.append(c_string_define("CROSSPOINT_VERSION", version))
    print(f"X3DP: CROSSPOINT_VERSION <- {origin} ({version})")

if defines:
    env.Append(CPPDEFINES=defines)  # noqa: F821
