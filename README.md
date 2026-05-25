# cppblog

> A lightweight CGI blog system written in pure C++ — no frameworks, no databases, no dependencies.

[![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Language: C++](https://img.shields.io/badge/Language-C%2B%2B11-orange.svg)]()
[![Storage: File-based](https://img.shields.io/badge/Storage-File--based-green.svg)]()

---

## ✨ Features

- **Zero dependencies** — one `.cpp` file, standard library only
- **File-based storage** — no MySQL, no SQLite, no Redis
- **Markdown support** — bold, italic, headings, code blocks, links, images, lists
- **Bilingual UI** — Chinese / English switchable via `?lang=zh` / `?lang=en`
- **Comment moderation** — all comments pending admin approval before display
- **Admin panel** — approve/reject/delete comments, edit/delete posts, bulk-clear all
- **Material-inspired design** — clean, responsive, mobile-friendly

---

## 📸 Screenshots

| Blog List | Post Detail | Admin Panel |
|-----------|-------------|-------------|
| *(add screenshot)* | *(add screenshot)* | *(add screenshot)* |

---

## 🚀 Quick Start

### Requirements

| Item | Requirement |
|------|-------------|
| Web server | Apache / Nginx with CGI support |
| Compiler | g++ with C++11 (`-std=c++11`) |
| OS | Linux (tested on Ubuntu 20.04+, Debian 11+) |
| Permissions | Write access to data directory |

### 1. Clone

```bash
git clone https://github.com/xpkgit/cppblog.git
cd cppblog
```

### 2. Configure

Open `cppblog.cpp` and edit the following constants at the top of the file:

```cpp
const string DATA_DIR    = "/usr/lib/cppblog/";   // data directory path
const string POSTS_FILE  = DATA_DIR + "/cppblog.md"; // posts storage file
const string ADMIN_PASSWORD = "YOUR_ADMIN_PASSWORD_HERE"; // ← change this!
```

> **Security:** Never commit your real password. See [SECURITY.md](SECURITY.md).

### 3. Compile

```bash
g++ -std=c++11 -O2 -o cppblog.cgi cppblog.cpp
```

### 4. Deploy (Apache)

```bash
# Copy binary to CGI directory
sudo cp cppblog.cgi /usr/lib/cgi-bin/cppblog.cgi

# Create data directory with correct permissions
sudo mkdir -p /usr/lib/cppblog/
sudo chown www-data:www-data /usr/lib/cppblog/
sudo chmod 755 /usr/lib/cppblog/
```

Enable CGI in Apache:

```bash
sudo a2enmod cgi
sudo systemctl restart apache2
```

Access: `http://your-server/cgi-bin/cppblog.cgi`

### 5. Deploy (Nginx + fcgiwrap)

```bash
sudo apt install fcgiwrap
sudo systemctl start fcgiwrap
```

Nginx config snippet:

```nginx
location /blog {
    fastcgi_pass  unix:/var/run/fcgiwrap.socket;
    include       fastcgi_params;
    fastcgi_param SCRIPT_FILENAME /usr/lib/cgi-bin/cppblog.cgi;
}
```

---

## 📁 Project Structure

```
cppblog/
├── cppblog.cpp          # Single-file source (all logic here)
├── README.md
├── SECURITY.md
├── CHANGELOG.md
├── LICENSE
└── .gitignore

# Runtime data (auto-created, NOT in repo)
/usr/lib/cppblog/
├── cppblog.md           # All posts (pipe-delimited)
└── reply_<id>.txt       # Per-post comment files
```

---

## 🔧 Configuration Reference

All configuration is done by editing constants in `cppblog.cpp` before compiling:

| Constant | Default | Description |
|----------|---------|-------------|
| `DATA_DIR` | `/usr/lib/cppblog/` | Directory for data files |
| `POSTS_FILE` | `DATA_DIR + "/cppblog.md"` | Posts storage file path |
| `ADMIN_PASSWORD` | `YOUR_ADMIN_PASSWORD_HERE` | Admin & write password |

---

## 🌐 URL Reference

| URL | Method | Description |
|-----|--------|-------------|
| `/?lang=zh` | GET | Blog list (Chinese) |
| `/?lang=en` | GET | Blog list (English) |
| `?a=post` | GET | Write new post (password required) |
| `?a=submit` | POST | Submit new post |
| `?a=view&id=<id>` | GET | View post detail |
| `?a=doreply&id=<id>` | POST | Submit comment |
| `?a=admin` | GET | Admin panel (password required) |
| `?a=adminlogin` | POST | Admin login |
| `?a=edit&id=<id>` | GET | Edit post form |
| `?a=update` | POST | Save edited post |
| `?a=approvereply&id=<id>` | GET | Approve comment |
| `?a=delreply&id=<id>` | GET | Delete comment |
| `?a=delblog&id=<id>` | GET | Delete post |
| `?a=deleteall` | POST | Delete all posts (admin only) |

---

## 📝 Data Format

### Posts file (`cppblog.md`)

Each line represents one post, fields separated by `|`:

```
<id>|<title>|<author>|<content>|<time>|<status>|<reply_count>|
```

- `status`: `approved` / `pending` / `rejected`
- Newlines in content are escaped as `\n`

### Comment file (`reply_<postId>.txt`)

```
<id>|<author>|<content>|<time>|<status>|
```

- `status`: `approved` / `pending`

---

## ✍️ Markdown Support

The built-in Markdown renderer supports:

| Syntax | Renders as |
|--------|-----------|
| `**bold**` | **bold** |
| `*italic*` | *italic* |
| `` `code` `` | `inline code` |
| ` ```code block``` ` | `<pre><code>` block |
| `# H1` / `## H2` / `### H3` / `#### H4` | Headings |
| `[text](url)` | Hyperlink |
| `![alt](url)` | Image |
| `- item` | Unordered list |
| `---` | Horizontal rule |

---

## 🔒 Security Notes

See [SECURITY.md](SECURITY.md) for full security guidelines.

**TL;DR:**
- Change `ADMIN_PASSWORD` before deploying
- Add `.gitignore` to prevent data files from being committed
- Do not expose the CGI binary's source URL
- Consider placing the data directory outside the web root

---

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Commit your changes: `git commit -m 'Add my feature'`
4. Push to the branch: `git push origin feature/my-feature`
5. Open a Pull Request

Please keep PRs focused and include a clear description of the change.

---

## 📄 License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

---

## 🙏 Acknowledgements

- [SimpleMDE](https://github.com/sparksuite/simplemde-markdown-editor) — Markdown editor in the browser
- [Noto Sans SC](https://fonts.google.com/noto/specimen/Noto+Sans+SC) — Chinese font via Google Fonts
- Material Design color palette for the UI
