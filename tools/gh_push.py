#!/usr/bin/env python3
"""
绕过 `git push`，直接用 GitHub REST API 提交本地改动。

`git push` 走代理时经常 "remote end hung up unexpectedly"，但 API 的 POST 是通的，
所以用 Blob → Tree → Commit → Ref 四步把本地 HEAD 推上去。

    python tools/gh_push.py --dry-run
    python tools/gh_push.py

需要环境变量 GH_TOKEN；仓库默认 z464244404/mochi。
"""
import argparse
import base64
import json
import os
import ssl
import subprocess
import sys
import urllib.request

REPO = "z464244404/mochi"
API = "https://api.github.com"
SSL_CTX = ssl.create_default_context()


def sh(*args):
    return subprocess.check_output(args, cwd=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))).decode().strip()


def api(method, path, body=None, token=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(API + path, data=data, method=method)
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("User-Agent", "mochi-push")
    req.add_header("Accept", "application/vnd.github+json")
    if data:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=60, context=SSL_CTX) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", "replace")
        sys.exit("%s %s -> HTTP %s\n%s" % (method, path, e.code, body[:800]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--insecure", action="store_true", help="跳过证书校验（本地代理 MITM 时需要）")
    ap.add_argument("--message")
    args = ap.parse_args()

    if args.insecure:
        global SSL_CTX
        SSL_CTX = ssl._create_unverified_context()

    token = os.environ.get("GH_TOKEN")
    if not token:
        sys.exit("缺少 GH_TOKEN")

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    remote = api("GET", f"/repos/{REPO}/git/ref/heads/main", token=token)
    base_sha = remote["object"]["sha"]
    local_head = sh("git", "rev-parse", "HEAD")

    if base_sha == local_head:
        print("远端已是最新，无需推送")
        return

    base_commit = api("GET", f"/repos/{REPO}/git/commits/{base_sha}", token=token)
    base_tree = base_commit["tree"]["sha"]

    changed = sh("git", "diff", "--name-only", f"{base_sha}..HEAD").split("\n")
    changed = [c for c in changed if c.strip()]
    print(f"远端 {base_sha[:7]} -> 本地 {local_head[:7]}，{len(changed)} 个文件")
    for c in changed:
        print("  ", c)
    if args.dry_run:
        return

    entries = []
    for path in changed:
        full = os.path.join(root, path)
        if not os.path.exists(full):
            entries.append({"path": path, "mode": "100644", "type": "blob", "sha": None})
            continue
        content = base64.b64encode(open(full, "rb").read()).decode()
        blob = api("POST", f"/repos/{REPO}/git/blobs", {"content": content, "encoding": "base64"}, token=token)
        entries.append({"path": path, "mode": "100644", "type": "blob", "sha": blob["sha"]})
        print("  blob", path, blob["sha"][:7])

    tree = api("POST", f"/repos/{REPO}/git/trees", {"base_tree": base_tree, "tree": entries}, token=token)
    msg = args.message or sh("git", "log", "-1", "--pretty=%B")
    commit = api(
        "POST",
        f"/repos/{REPO}/git/commits",
        {"message": msg, "tree": tree["sha"], "parents": [base_sha]},
        token=token,
    )
    api("PATCH", f"/repos/{REPO}/git/refs/heads/main", {"sha": commit["sha"]}, token=token)
    print("推送完成:", commit["sha"][:7])


if __name__ == "__main__":
    main()
