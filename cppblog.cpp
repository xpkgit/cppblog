/**
 * C++ CGI博客程序 - 发博客/编辑博客/回复审核
 * 使用文件存储数据
 */

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <ctime>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

using namespace std;

const string DATA_DIR = "/usr/lib/cppblog/";
const string SESSION_DIR = DATA_DIR + "sessions/";
const string POSTS_FILE = DATA_DIR + "/cppblog.md";
const string ADMIN_PASSWORD = "YOUR_ADMIN_PASSWORD_HERE";  // TODO: 部署前替换为强密码，勿提交真实密码到版本库

// 会话有效期（秒）
const long SESSION_TTL = 7200;

// POST请求主体大小上限（字节），防止无界读取导致内存耗尽
const size_t MAX_BODY = 1048576;

// 本次响应需要下发的Cookie（用于会话token）
vector<string> g_outCookies;

// 获取环境变量
const char* env(const char* name) {
    return getenv(name) ? getenv(name) : "";
}

// URL解码
string decode(const string& s) {
    string r;
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '%' && i + 2 < s.length()) {
            int v = 0;                       // 初始化，避免读取未定义值
            // 仅当后两位确为十六进制且成功解析时才解码，否则原样保留
            if (isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])
                && sscanf(s.c_str() + i + 1, "%2x", &v) == 1) {
                r += char(v);
                i += 2;
            } else {
                r += s[i];
            }
        } else if (s[i] == '+') {
            r += ' ';
        } else {
            r += s[i];
        }
    }
    return r;
}

// HTML转义
string escape(const string& s) {
    string r;
    for (char c : s) {
        if (c == '<') r += "&lt;";
        else if (c == '>') r += "&gt;";
        else if (c == '&') r += "&amp;";
        else if (c == '"') r += "&quot;";
        else r += c;
    }
    return r;
}

// 校验链接URL安全：仅放行 http/https/mailto 与站内相对路径，阻断 javascript:/data: 等XSS协议
bool isSafeUrl(const string& rawUrl) {
    string url = decode(rawUrl);        // 解码后再判断，防止 javascript%3A 之类绕过
    if (url.empty()) return true;
    string lower;
    for (size_t i = 0; i < url.length(); ++i) lower += (char)tolower((unsigned char)url[i]);
    size_t schemeEnd = lower.find(':');
    if (schemeEnd == string::npos) return true;   // 无协议头部，视为相对路径/锚点
    string scheme = lower.substr(0, schemeEnd);
    return (scheme == "http" || scheme == "https" || scheme == "mailto");
}

// 简单的Markdown转HTML
string md2html(const string& md) {
    string s = escape(md);
    
    // 代码块 ```...```
    size_t pos = 0;
    while ((pos = s.find("```", pos)) != string::npos) {
        size_t end = s.find("```", pos + 3);
        if (end == string::npos) break;
        string code = s.substr(pos + 3, end - pos - 3);
        s.replace(pos, end - pos + 3, "<pre><code>" + code + "</code></pre>");
        pos = end + 3;
    }
    
    // 行内代码 `...`
    pos = 0;
    while ((pos = s.find("`", pos)) != string::npos) {
        size_t end = s.find("`", pos + 1);
        if (end == string::npos) break;
        string code = s.substr(pos + 1, end - pos - 1);
        s.replace(pos, end - pos + 1, "<code>" + code + "</code>");
        pos = end + 1;
    }
    
    // 粗体 **...**
    pos = 0;
    while ((pos = s.find("**", pos)) != string::npos) {
        size_t end = s.find("**", pos + 2);
        if (end == string::npos) break;
        string text = s.substr(pos + 2, end - pos - 2);
        s.replace(pos, end - pos + 2, "<strong>" + text + "</strong>");
        pos = end + 2;
    }
    
    // 斜体 *...*
    pos = 0;
    while ((pos = s.find("*", pos)) != string::npos) {
        size_t end = s.find("*", pos + 1);
        if (end == string::npos) break;
        string text = s.substr(pos + 1, end - pos - 1);
        if (text.find("<") == string::npos) {
            s.replace(pos, end - pos + 1, "<em>" + text + "</em>");
            pos = end + 1;
        } else {
            pos++;
        }
    }
    
    // 图片 ![alt](url) —— 必须先于链接规则处理，否则 "![" 会被链接规则提前吞掉导致图片永不渲染
    pos = 0;
    while ((pos = s.find("![", pos)) != string::npos) {
        size_t end = s.find("](", pos);
        size_t urlEnd = s.find(")", end + 2);
        if (end == string::npos || urlEnd == string::npos) break;
        string alt = s.substr(pos + 2, end - pos - 2);
        string url = s.substr(end + 2, urlEnd - end - 2);
        if (!isSafeUrl(url)) {
            // 危险协议：降级为纯文本，杜绝 <img onerror> 类事件注入
            s.replace(pos, urlEnd - pos + 1, "<span>[" + alt + "]</span>");
        } else {
            s.replace(pos, urlEnd - pos + 1, "<img src=\"" + url + "\" alt=\"" + alt + "\" style=\"max-width:100%;\">");
        }
        pos = urlEnd + 1;
    }

    // 链接 [text](url)
    pos = 0;
    while ((pos = s.find("[", pos)) != string::npos) {
        size_t end = s.find("](", pos);
        size_t urlEnd = s.find(")", end + 2);
        if (end == string::npos || urlEnd == string::npos) break;
        // 跳过已渲染的图片/链接内部产生的方括号，避免二次解析
        size_t lt = s.rfind('<', pos);
        if (lt != string::npos) {
            size_t gt = s.find('>', lt);
            if (gt != string::npos && gt > pos) { pos = gt + 1; continue; }
        }
        string text = s.substr(pos + 1, end - pos - 1);
        string url = s.substr(end + 2, urlEnd - end - 2);
        if (!isSafeUrl(url)) url = "#";          // 阻断 javascript:/data: 等危险协议
        s.replace(pos, urlEnd - pos + 1, "<a href=\"" + url + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + text + "</a>");
        pos = urlEnd + 1;
    }
    
    // 标题 ### ...
    string out;
    stringstream ss(s);
    string line;
    while (getline(ss, line)) {
        if (line.substr(0, 4) == "####") {
            out += "<h4>" + line.substr(4) + "</h4>";
        } else if (line.substr(0, 3) == "###") {
            out += "<h3>" + line.substr(3) + "</h3>";
        } else if (line.substr(0, 2) == "##") {
            out += "<h2>" + line.substr(2) + "</h2>";
        } else if (line.substr(0, 1) == "#") {
            out += "<h1>" + line.substr(1) + "</h1>";
        } else if (line.substr(0, 3) == "---") {
            out += "<hr>";
        } else {
            out += line + "\n";
        }
    }
    
    // 换行处理
    size_t lp = 0;
    while ((lp = out.find("\n\n", lp)) != string::npos) {
        out.replace(lp, 2, "</p><p>");
        lp += 10;
    }
    if (out.find("<p>") != 0 && !out.empty()) {
        out = "<p>" + out + "</p>";
    }
    
    // 列表 - ...
    pos = 0;
    bool inList = false;
    string result;
    ss.clear();
    ss.str(out);
    while (getline(ss, line)) {
        if (line.find("- ") == 0) {
            if (!inList) {
                result += "<ul>";
                inList = true;
            }
            result += "<li>" + line.substr(2) + "</li>";
        } else {
            if (inList) {
                result += "</ul>";
                inList = false;
            }
            result += line;
        }
    }
    if (inList) result += "</ul>";
    
    return result;
}

// 解析参数：按 & 分段后严格比对键名，杜绝 "xa=1" 被当作 "a=1" 的前缀劫持
string getq(const string& qs, const string& key) {
    string d = decode(qs);
    string k = key + "=";
    size_t start = 0;
    while (start <= d.length()) {
        size_t amp = d.find('&', start);
        size_t end = (amp == string::npos) ? d.length() : amp;
        string seg = d.substr(start, end - start);
        // 段首必须精确等于 key=，避免其它参数名以其为后缀时误命中
        if (seg.length() >= k.length() && seg.compare(0, k.length(), k) == 0) {
            return seg.substr(k.length());
        }
        if (amp == string::npos) break;
        start = amp + 1;
    }
    return "";
}

// 读取POST请求主体：依据CONTENT_LENGTH限量读取，避免无界内存消耗(DoS)
string readPostBody(size_t maxBytes) {
    string body;
    const char* cl = env("CONTENT_LENGTH");
    long len = (cl && *cl) ? atol(cl) : 0;
    if (len <= 0) return body;
    if ((size_t)len > maxBytes) len = (long)maxBytes;   // 超限部分直接丢弃
    body.reserve((size_t)len);
    for (long i = 0; i < len; ++i) {
        int ch = cin.get();
        if (ch == EOF) break;
        body += (char)ch;
    }
    return body;
}

// 获取当前时间
string now() {
    time_t t = time(NULL);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    return buf;
}

// 生成ID
string genId() {
    time_t t = time(NULL);
    stringstream ss;
    ss << t << (rand() % 10000);
    return ss.str();
}

// ============ 安全加固：输入校验 / 会话管理 ============

// 校验帖子ID合法：仅允许纯数字，杜绝路径穿越与命令注入
bool isValidId(const string& id) {
    if (id.empty() || id.length() > 32) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        if (!isdigit((unsigned char)id[i])) return false;
    }
    return true;
}

// 清洗用户输入：去除换行与管道符，防止存储格式(行记录)注入
string sanitizeField(const string& s) {
    string r;
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (c == '\n' || c == '\r' || c == '|') r += ' ';
        else r += c;
    }
    return r;
}

// 清洗正文：保留 Markdown 换行，仅剔除记录分隔符 '|'
string sanitizeContent(const string& s) {
    string r;
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (c == '|') r += ' ';
        else r += c;
    }
    return r;
}

// 由帖子ID推导回复文件路径（已校验合法性）
bool replyFilePath(const string& postId, string& outPath) {
    if (!isValidId(postId)) return false;
    outPath = DATA_DIR + "reply_" + postId + ".txt";
    return true;
}

// 容错清理ID：只保留数字与下划线，遇到其它字符即截断
// 用于应对 "id=123?lang=en" 这类把第二个分隔符误写成 ? 的URL
string normalizeId(const string& raw) {
    string r;
    for (size_t i = 0; i < raw.length(); ++i) {
        char c = raw[i];
        if ((c >= '0' && c <= '9') || c == '_') r += c;
        else break;
    }
    return r;
}

// 读取Cookie中的指定字段：按 ';' 分段严格匹配键名，防止 xcppblog_sid 之类的前缀劫持
string getCookieValue(const char* name) {
    const char* raw = env("HTTP_COOKIE");
    if (!raw) return "";
    string cookie = raw;
    string n = string(name) + "=";
    size_t start = 0;
    while (start <= cookie.length()) {
        size_t sep = cookie.find(';', start);
        size_t end = (sep == string::npos) ? cookie.length() : sep;
        string seg = cookie.substr(start, end - start);
        size_t b = 0;
        while (b < seg.length() && seg[b] == ' ') b++;   // 去除前导空格
        seg = seg.substr(b);
        if (seg.length() > n.length() && seg.compare(0, n.length(), n) == 0) {
            return decode(seg.substr(n.length()));
        }
        if (sep == string::npos) break;
        start = sep + 1;
    }
    return "";
}

// 校验会话token自身格式（防止拼路径）
bool isWellFormedToken(const string& token) {
    if (token.length() != 32) return false;
    for (size_t i = 0; i < token.length(); ++i) {
        char c = token[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// 生成随机会话token
string genToken() {
    ifstream urandom("/dev/urandom", ios::binary);
    if (urandom) {
        static const char hexChars[] = "0123456789abcdef";
        string token;
        for (int i = 0; i < 32; ++i) {
            unsigned char b = 0;
            urandom.read((char*)&b, 1);
            token += hexChars[b % 16];
        }
        return token;
    }
    // 回退方案：时间+进程+随机数混合
    stringstream ss;
    ss << hex << time(NULL) << clock() << getpid() << rand();
    string token = ss.str();
    while (token.length() < 32) token += '0';
    return token.substr(0, 32);
}

// 创建服务端会话，返回token（密码不再出现在URL中）
// 使用 O_CREAT|O_EXCL 排他创建 + 0600权限，避免覆盖他人会话或会话文件被同主机其它用户读取
string createSession() {
    mkdir(SESSION_DIR.c_str(), 0700);
    string token;
    for (int i = 0; i < 8; ++i) {
        token = genToken();
        string path = SESSION_DIR + token;
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            string ts = to_string((long)time(NULL)) + "\n";
            write(fd, ts.c_str(), ts.length());
            close(fd);
            return token;
        }
    }
    return token;
}

// 校验会话是否有效（含过期清理）
bool isValidSession(const string& token) {
    if (!isWellFormedToken(token)) return false;
    string path = SESSION_DIR + token;
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    ifstream f(path.c_str());
    long created = 0;
    f >> created;
    if (created <= 0) return false;
    long age = (long)time(NULL) - created;
    if (age < 0) age = -age;
    if (age > SESSION_TTL) {
        f.close();
        unlink(path.c_str());
        return false;
    }
    return true;
}

// 销毁会话
void destroySession(const string& token) {
    if (!isWellFormedToken(token)) return;
    unlink((SESSION_DIR + token).c_str());
}

// 当前请求是否为已登录管理员
bool isAdminAuthed() {
    return isValidSession(getCookieValue("cppblog_sid"));
}

// 下发HttpOnly会话Cookie
void setSessionCookie(const string& token) {
    g_outCookies.push_back("Set-Cookie: cppblog_sid=" + token +
                           "; Path=/; HttpOnly; SameSite=Strict\r\n");
}

// 使会话Cookie失效
void clearSessionCookie() {
    g_outCookies.push_back("Set-Cookie: cppblog_sid=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0\r\n");
}

// 帖子结构
struct Post {
    string id, title, author, content, time, status, reply_count;
};

// 序列化帖子 - 将换行替换为\\n
string serializePost(const Post& p) {
    string content = p.content;
    size_t pos;
    while ((pos = content.find('\n')) != string::npos) {
        content.replace(pos, 1, "\\n");
    }
    return p.id + "|" + p.title + "|" + p.author + "|" + content + "|" + 
           p.time + "|" + p.status + "|" + p.reply_count + "|";
}

// 反序列化帖子 - 将\\n还原为换行
Post parsePost(const string& line) {
    Post p;
    stringstream ss(line);
    string field;
    vector<string> fields;
    while (getline(ss, field, '|')) {
        fields.push_back(field);
    }
    if (fields.size() >= 7) {
        p.id = fields[0];
        p.title = fields[1];
        p.author = fields[2];
        p.content = fields[3];
        size_t pos;
        while ((pos = p.content.find("\\n")) != string::npos) {
            p.content.replace(pos, 2, "\n");
        }
        p.time = fields[4];
        p.status = fields[5];
        p.reply_count = fields[6];
    }
    return p;
}

// 读取所有帖子
vector<Post> loadPosts() {
    vector<Post> posts;
    ifstream f(POSTS_FILE);
    string line;
    while (getline(f, line)) {
        if (!line.empty()) {
            posts.push_back(parsePost(line));
        }
    }
    reverse(posts.begin(), posts.end());
    return posts;
}

// 保存帖子
void savePosts(const vector<Post>& posts) {
    ofstream f(POSTS_FILE);
    for (auto it = posts.rbegin(); it != posts.rend(); ++it) {
        f << serializePost(*it) << endl;
    }
}

// 添加回复 - 默认待审核
void addReply(const string& postId, const string& author, const string& content) {
    string replyFile;
    if (!replyFilePath(postId, replyFile)) return;      // 非法ID，拒绝写入
    string safeAuthor = sanitizeField(author);
    string safeContent = sanitizeField(content);
    if (safeAuthor.empty()) safeAuthor = "匿名";
    if (safeContent.empty()) return;
    ofstream f(replyFile.c_str(), ios::app);
    f << genId() << "|" << safeAuthor << "|" << safeContent << "|" << now() << "|pending|" << endl;
}

// 加载回复 - 参数: postId, showPending(是否显示待审核)
struct Reply {
    string id, author, content, time, status;
};

vector<Reply> loadReplies(const string& postId, bool approvedOnly = true) {
    vector<Reply> replies;
    string replyFile;
    if (!replyFilePath(postId, replyFile)) return replies;   // 非法ID，返回空列表
    ifstream f(replyFile.c_str());
    string line;
    while (getline(f, line)) {
        if (!line.empty()) {
            stringstream ss(line);
            string field;
            vector<string> fields;
            while (getline(ss, field, '|')) {
                fields.push_back(field);
            }
            if (fields.size() >= 5) {
                Reply r;
                r.id = fields[0];
                r.author = fields[1];
                r.content = fields[2];
                r.time = fields[3];
                r.status = fields[4];
                if (!approvedOnly || r.status == "approved") {
                    replies.push_back(r);
                }
            }
        }
    }
    return replies;
}

// 更新回复数(只统计已通过的)
void updateReplyCount(const string& postId) {
    vector<Post> posts = loadPosts();
    for (auto& p : posts) {
        if (p.id == postId) {
            vector<Reply> replies = loadReplies(postId, true);
            p.reply_count = to_string(replies.size());
            break;
        }
    }
    savePosts(posts);
}

// 列出所有回复文件路径：改用目录遍历，替代 popen("ls") 外部命令
vector<string> listReplyFiles() {
    vector<string> files;
    DIR* dir = opendir(DATA_DIR.c_str());
    if (!dir) return files;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        string name = ent->d_name;
        if (name.compare(0, 6, "reply_") == 0 && name.length() > 4 &&
            name.compare(name.length() - 4, 4, ".txt") == 0) {
            files.push_back(DATA_DIR + name);
        }
    }
    closedir(dir);
    return files;
}

// 加载所有待审核的回复
vector<Reply> loadAllPendingReplies() {
    vector<Reply> allReplies;
    vector<string> replyFiles = listReplyFiles();
    for (size_t fi = 0; fi < replyFiles.size(); ++fi) {
        {
            string f = replyFiles[fi];
            ifstream rf(f);
            string line;
            while (getline(rf, line)) {
                if (!line.empty()) {
                    stringstream ss(line);
                    string field;
                    vector<string> fields;
                    while (getline(ss, field, '|')) {
                        fields.push_back(field);
                    }
                    if (fields.size() >= 5 && fields[4] == "pending") {
                        Reply r;
                        r.id = fields[0];
                        r.author = fields[1];
                        r.content = fields[2];
                        r.time = fields[3];
                        r.status = fields[4];
                        // 从文件名提取postId
                        size_t s = f.find("reply_");
                        size_t e = f.find(".txt");
                        if (s != string::npos && e != string::npos) {
                            r.id = f.substr(s + 6, e - s - 6) + "_" + r.id;
                        }
                        allReplies.push_back(r);
                    }
                }
            }
            rf.close();
        }
    }
    return allReplies;
}

// 审核回复
void approveReply(const string& replyFullId) {
    size_t sep = replyFullId.find('_');
    if (sep == string::npos) return;
    string postId = replyFullId.substr(0, sep);
    string replyId = replyFullId.substr(sep + 1);
    string replyFile;
    if (!replyFilePath(postId, replyFile)) return;      // 非法ID，拒绝操作

    ifstream rf(replyFile);
    vector<string> lines;
    string line;
    while (getline(rf, line)) {
        if (line.find(replyId + "|") == 0) {
            size_t p = line.find("|pending|");
            if (p != string::npos) {
                line.replace(p, 9, "|approved|");
            }
        }
        lines.push_back(line);
    }
    rf.close();
    
    ofstream wf(replyFile);
    for (auto& l : lines) {
        wf << l << endl;
    }
    
    updateReplyCount(postId);
}

// 删除回复
void deleteReply(const string& replyFullId) {
    size_t sep = replyFullId.find('_');
    if (sep == string::npos) return;
    string postId = replyFullId.substr(0, sep);
    string replyId = replyFullId.substr(sep + 1);
    string replyFile;
    if (!replyFilePath(postId, replyFile)) return;      // 非法ID，拒绝操作

    ifstream rf(replyFile);
    vector<string> lines;
    string line;
    while (getline(rf, line)) {
        if (line.find(replyId + "|") != 0) {
            lines.push_back(line);
        }
    }
    rf.close();
    
    ofstream wf(replyFile);
    for (auto& l : lines) {
        wf << l << endl;
    }
}

// 更新博客
void updatePost(const string& id, const string& title, const string& content) {
    if (!isValidId(id)) return;          // 非法ID，拒绝修改
    vector<Post> posts = loadPosts();
    for (auto& p : posts) {
        if (p.id == id) {
            p.title = sanitizeField(title);
            p.content = sanitizeContent(content);
            break;
        }
    }
    savePosts(posts);
}

// 删除博客
void deleteBlog(const string& id) {
    if (!isValidId(id)) return;          // ID非法即拒绝，阻断命令注入与路径穿越
    vector<Post> posts = loadPosts();
    vector<Post> newPosts;
    for (auto& p : posts) {
        if (p.id != id) {
            newPosts.push_back(p);
        }
    }
    savePosts(newPosts);
    // 删除该博客的回复文件：直接调用unlink，杜绝shell命令拼接
    string replyFile = DATA_DIR + "reply_" + id + ".txt";
    unlink(replyFile.c_str());
}

void deleteAllPosts() {
    // 清空所有帖子
    ofstream f(POSTS_FILE, ios::trunc);
    f.close();
    // 删除所有回复文件：遍历目录逐个unlink，替代 system("rm -f") 外部命令
    vector<string> replyFiles = listReplyFiles();
    for (size_t i = 0; i < replyFiles.size(); ++i) {
        unlink(replyFiles[i].c_str());
    }
}

// HTML页面 - Material-UI浅色极简风格
void html(const string& title, const string& lang, const string& content) {
    string langSwitch = (lang == "en") ? "<a href=\"?lang=zh\" style=\"position:fixed;top:16px;right:20px;padding:6px 12px;background:#fff;border-radius:4px;text-decoration:none;color:#1976d2;font-size:0.85rem;box-shadow:0 1px 3px rgba(0,0,0,0.1);\">中文</a>" : "<a href=\"?lang=en\" style=\"position:fixed;top:16px;right:20px;padding:6px 12px;background:#fff;border-radius:4px;text-decoration:none;color:#1976d2;font-size:0.85rem;box-shadow:0 1px 3px rgba(0,0,0,0.1);\">English</a>";
    cout << "Content-type: text/html; charset=utf-8\r\n";
    cout << "Cache-Control: no-store\r\n";           // 管理页面禁止缓存
    cout << "X-Content-Type-Options: nosniff\r\n";
    cout << "X-Frame-Options: SAMEORIGIN\r\n";       // 防点击劫持
    cout << "Referrer-Policy: no-referrer\r\n";      // 避免URL经Referer外泄
    for (size_t i = 0; i < g_outCookies.size(); ++i) cout << g_outCookies[i];
    cout << "\r\n";
    cout << R"(<!DOCTYPE html>
<html lang=")" << (lang == "en" ? "en" : "zh-CN") << R"(">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>)" << title << R"(</title>
<link href="https://fonts.googleapis.com/css2?family=Noto+Sans+SC:wght@300;400;500;700&display=swap" rel="stylesheet">
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: 'Noto Sans SC', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; 
       background: #fafafa; min-height: 100vh; color: #333; line-height: 1.6; }
.container { max-width: 800px; margin: 0 auto; padding: 40px 20px; }
.header { text-align: center; margin-bottom: 48px; padding: 20px 0; }
.header h1 { font-size: 2rem; font-weight: 300; color: #1a1a1a; letter-spacing: 2px; margin-bottom: 8px; }
.header p { font-size: 0.95rem; color: #757575; font-weight: 300; }
.nav { display: flex; justify-content: center; gap: 8px; margin-bottom: 40px; padding: 16px; 
       background: white; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.08); }
.nav a { padding: 10px 24px; color: #555; text-decoration: none; font-size: 0.9rem; font-weight: 400;
         border-radius: 4px; transition: all 0.2s; }
.nav a:hover { background: #f5f5f5; color: #1a1a1a; }
.nav a.active { background: #1976d2; color: white; }
.post { background: white; border-radius: 8px; padding: 32px; margin-bottom: 24px;
        box-shadow: 0 1px 3px rgba(0,0,0,0.08); transition: box-shadow 0.2s; }
.post:hover { box-shadow: 0 2px 8px rgba(0,0,0,0.12); }
.post-title { font-size: 1.4rem; font-weight: 500; color: #1a1a1a; margin-bottom: 12px; line-height: 1.4; }
.post-meta { font-size: 0.85rem; color: #9e9e9e; margin-bottom: 16px; font-weight: 300; }
.post-content { color: #444; font-size: 0.95rem; line-height: 1.8; }
.post-pending { border-left: 4px solid #ff9800; }
.post-rejected { border-left: 4px solid #f44336; }
.status { display: inline-block; padding: 2px 10px; border-radius: 12px; font-size: 0.75rem; color: white; margin-right: 12px; }
.status-approved { background: #4caf50; }
.status-pending { background: #ff9800; }
.status-rejected { background: #f44336; }
input, textarea { width: 100%; padding: 14px 16px; border: 1px solid #e0e0e0; 
                  border-radius: 6px; background: white; color: #333; font-size: 0.95rem; 
                  font-family: inherit; transition: border-color 0.2s; }
input:focus, textarea:focus { outline: none; border-color: #1976d2; }
textarea { min-height: 200px; resize: vertical; font-family: 'Monaco', 'Menlo', monospace; }
label { display: block; margin-bottom: 8px; color: #424242; font-size: 0.9rem; font-weight: 400; }
button { padding: 12px 32px; background: #1976d2; color: white; border: none; border-radius: 6px; 
         font-size: 0.9rem; font-weight: 500; cursor: pointer; transition: background 0.2s; font-family: inherit; }
button:hover { background: #1565c0; }
button:active { background: #0d47a1; }
.btn-outline { background: white; color: #1976d2; border: 1px solid #1976d2; }
.btn-outline:hover { background: #e3f2fd; }
.btn-danger { background: #d32f2f; }
.btn-danger:hover { background: #c62828; }
.btn-small { padding: 6px 16px; font-size: 0.85rem; }
.actions { display: flex; gap: 8px; margin-top: 20px; }
.empty { text-align: center; padding: 60px 20px; color: #9e9e9e; font-size: 0.95rem; }
.footer { text-align: center; padding: 40px 20px; color: #bdbdbd; font-size: 0.8rem; margin-top: 40px; }
.error { background: #ffebee; color: #c62828; padding: 16px 20px; border-radius: 6px; margin: 16px 0; font-size: 0.9rem; }
.success { background: #e8f5e9; color: #2e7d32; padding: 16px 20px; border-radius: 6px; margin: 16px 0; font-size: 0.9rem; }
.md-content { line-height: 1.8; }
.md-content h1, .md-content h2, .md-content h3, .md-content h4 { color: #1a1a1a; margin: 24px 0 12px; font-weight: 500; }
.md-content h1 { font-size: 1.5rem; }
.md-content h2 { font-size: 1.3rem; }
.md-content h3 { font-size: 1.15rem; }
.md-content code { background: #f5f5f5; padding: 2px 6px; border-radius: 3px; color: #d32f2f; font-size: 0.9em; }
.md-content pre { background: #f5f5f5; padding: 16px; border-radius: 6px; overflow-x: auto; margin: 16px 0; }
.md-content pre code { background: none; padding: 0; color: #333; }
.md-content a { color: #1976d2; text-decoration: none; }
.md-content a:hover { text-decoration: underline; }
.md-content blockquote { border-left: 3px solid #1976d2; padding-left: 16px; margin: 16px 0; color: #666; }
</style>
</head>
<body>
)" << langSwitch << R"(
<div class="container">
)" << content << R"(
<div class="footer">
<p>My Blog | )" << now() << R"(</p>
</div>
</div>
</body>
</html>)";
}

// 首页/博客列表
string renderList(const vector<Post>& posts, const string& lang = "zh") {
    bool en = (lang == "en");
    string tBlog = en ? "My Blog" : "我的博客";
    string tDesc = en ? "Share thoughts, record life" : "记录生活，分享思考";
    string tPosts = en ? "Posts" : "博客列表";
    string tWrite = en ? "Write" : "写博客";
    string tAdmin = en ? "Admin" : "管理";
    string tReadMore = en ? "Read More" : "阅读全文";
    string tEmpty = en ? "No posts yet, " : "暂无博客，";
    string tStart = en ? "start writing" : "开始写博客";
    
    stringstream ss;
    ss << "<div class=\"header\">";
    ss << "<h1>" << tBlog << "</h1>";
    ss << "<p>" << tDesc << "</p>";
    ss << "</div>";
    ss << "<div class=\"nav\">";
    ss << "<a href=\"/?lang=" << lang << "\" class=\"active\">" << tPosts << "</a>";
    ss << "<a href=\"?a=post&lang=" << lang << "\">" << tWrite << "</a>";
    ss << "<a href=\"?a=admin&lang=" << lang << "\">" << tAdmin << "</a>";
    ss << "</div>";

    int count = 0;
    for (const auto& p : posts) {
        if (p.status != "approved") continue;
        count++;
        
        ss << "<div class=\"post\">";
        ss << "<div class=\"post-title\">" << escape(p.title) << "</div>";
        ss << "<div class=\"post-meta\">" << p.time << " &middot; " << escape(p.author) << "</div>";
        // 先在Markdown源层面截断再渲染，避免在HTML中间切断标签导致结构破损
        string summarySrc = p.content.length() > 300 ? p.content.substr(0, 300) : p.content;
        ss << "<div class=\"post-content\">" << md2html(summarySrc);
        if (p.content.length() > 300) ss << "...";
        ss << "</div>";
        ss << "<div class=\"actions\">";
        ss << "<a href=\"?a=view&id=" << p.id << "&lang=" << lang << "\" class=\"btn-small btn-outline\">" << tReadMore << "</a>";
        ss << "</div></div>";
    }

    if (count == 0) {
        ss << "<div class=\"empty\">" << tEmpty << "<a href=\"?a=post&lang=" << lang << "\">" << tStart << "</a></div>";
    }

    return ss.str();
}

// 博客密码验证表单
string renderBlogLoginForm(const string& error = "", const string& purpose = "blog", const string& lang = "zh") {
    bool en = (lang == "en");
    string title = (purpose == "admin") ? (en ? "Admin Panel" : "管理面板") : (en ? "Write Blog" : "写博客");
    string action = (purpose == "admin") ? "adminlogin" : "bloglogin";
    string tDesc = en ? "Enter password to verify" : "请输入密码验证身份";
    string tPwd = en ? "Password" : "密码";
    string tPlaceholder = en ? "Enter blog password" : "请输入博客密码";
    string tVerify = en ? "Verify" : "验证";
    string tBack = en ? "< Back to Blog" : "← 返回博客";
    
    stringstream ss;
    ss << "<div class=\"header\">";
    ss << "<h1>" << title << "</h1>";
    ss << "<p>" << tDesc << "</p>";
    ss << "</div>";
    ss << "<div class=\"post\">";
    ss << "<form action=\"?a=" << action << "&lang=" << lang << "\" method=\"post\">";
    if (!error.empty()) {
        ss << "<div class=\"error\">" << error << "</div>";
    }
    ss << "<div style=\"margin-bottom:20px\">";
    ss << "<label>" << tPwd << "</label>";
    ss << "<input type=\"password\" name=\"password\" placeholder=\"" << tPlaceholder << "\" required>";
    ss << "</div>";
    ss << "<button type=\"submit\">" << tVerify << "</button>";
    ss << "</form>";
    ss << "</div>";
    ss << "<div style=\"text-align:center;margin-top:20px;\">";
    ss << "<a href=\"/?lang=" << lang << "\" style=\"color:#757575;\">" << tBack << "</a>";
    ss << "</div>";
    return ss.str();
}

// 发博客表单
string renderPostForm(const string& lang = "zh") {
    bool en = (lang == "en");
    string tTitle = en ? "Write Blog" : "写博客";
    string tDesc = en ? "Share your thoughts" : "分享你的想法";
    string tBlog = en ? "Blog" : "博客";
    string tAdmin = en ? "Admin" : "管理";
    string tLabelTitle = en ? "Title" : "标题";
    string tTitlePlaceholder = en ? "Enter blog title" : "输入博客标题";
    string tLabelContent = en ? "Content (Markdown supported)" : "正文 (支持 Markdown)";
    string tContentPlaceholder = en ? "Write something..." : "写点什么...";
    string tSubmit = en ? "Publish" : "发布博客";
    string tBack = en ? "< Back to Blog" : "← 返回博客";
    
    string html = "<div class=\"header\">";
    html += "<h1>" + tTitle + "</h1>";
    html += "<p>" + tDesc + "</p>";
    html += "</div>";
    html += "<div class=\"nav\"><a href=\"/?lang=" + lang + "\">" + tBlog + "</a><a href=\"?a=admin&lang=" + lang + "\">" + tAdmin + "</a></div>";
    html += "<div class=\"post\">";
    html += "<form action=\"?a=submit&lang=" + lang + "\" method=\"post\" id=\"blogForm\">";
    html += "<div style=\"margin-bottom:20px\">";
    html += "<label>" + tLabelTitle + "</label>";
    html += "<input name=\"title\" id=\"blog-title\" placeholder=\"" + tTitlePlaceholder + "\" required>";
    html += "</div>";
    html += "<div style=\"margin-bottom:20px\">";
    html += "<label>" + tLabelContent + "</label>";
    html += "<textarea name=\"content\" id=\"blog-editor\" placeholder=\"" + tContentPlaceholder + "\" style=\"min-height:300px;\" required></textarea>";
    html += "</div>";
    html += "<button type=\"submit\" onclick=\"syncEditor()\">" + tSubmit + "</button>";
    html += "</form></div>";
    html += "<div style=\"text-align:center;margin-top:20px;\">";
    html += "<a href=\"/?lang=" + lang + "\" style=\"color:#757575;\">" + tBack + "</a>";
    html += "</div>";
    html += "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/simplemde@1.11.2/dist/simplemde.min.css\">";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/simplemde@1.11.2/dist/simplemde.min.js\"></script>";
    html += "<script>var mde;function syncEditor(){document.getElementById('blog-editor').value=mde.value();}document.addEventListener('DOMContentLoaded',function(){mde=new SimpleMDE({element:document.getElementById('blog-editor'),spellChecker:false,status:false,toolbar:['bold','italic','|','heading','|','quote','code','unordered-list','|','link','image','|','preview','side-by-side']});});</script>";
    return html;
}

// 编辑博客表单
string renderEditForm(const string& id, const string& lang = "zh") {
    bool en = (lang == "en");
    vector<Post> posts = loadPosts();
    Post* pPtr = nullptr;
    for (auto& p : posts) {
        if (p.id == id) {
            pPtr = &p;
            break;
        }
    }
    if (!pPtr) {
        return "<div class=\"error\">" + string(en ? "Blog not found" : "博客不存在") + "</div>";
    }
    
    string tTitle = en ? "Edit Blog" : "编辑博客";
    string tBlog = en ? "Blog" : "博客";
    string tAdmin = en ? "Admin" : "管理";
    string tLabelTitle = en ? "Title" : "标题";
    string tLabelContent = en ? "Content (Markdown)" : "正文 (支持 Markdown)";
    string tSubmit = en ? "Save Changes" : "保存修改";
    string tBack = en ? "< Back to Blog" : "← 返回博客";
    
    stringstream ss;
    ss << "<div class=\"header\"><h1>" << tTitle << "</h1></div>";
    ss << "<div class=\"nav\"><a href=\"/?lang=" << lang << "\">" << tBlog << "</a><a href=\"?a=admin&lang=" << lang << "\">" << tAdmin << "</a></div>";
    ss << "<div class=\"post\">";
    ss << "<form action=\"?a=update&lang=" << lang << "\" method=\"post\" id=\"editForm\">";
    ss << "<input type=\"hidden\" name=\"id\" value=\"" << escape(id) << "\">";
    ss << "<div style=\"margin-bottom:20px\">";
    ss << "<label>" << tLabelTitle << "</label>";
    ss << "<input name=\"title\" id=\"edit-title\" value=\"" << escape(pPtr->title) << "\" required>";
    ss << "</div>";
    ss << "<div style=\"margin-bottom:20px\">";
    ss << "<label>" << tLabelContent << "</label>";
    ss << "<textarea name=\"content\" id=\"edit-editor\" style=\"min-height:300px;\" required>" << escape(pPtr->content) << "</textarea>";
    ss << "</div>";
    ss << "<button type=\"submit\" onclick=\"syncEdit()\">" << tSubmit << "</button>";
    ss << "</form></div>";
    ss << "<div style=\"text-align:center;margin-top:20px;\">";
    ss << "<a href=\"/?lang=" << lang << "\" style=\"color:#757575;\">" << tBack << "</a>";
    ss << "</div>";
    ss << "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/simplemde@1.11.2/dist/simplemde.min.css\">";
    ss << "<script src=\"https://cdn.jsdelivr.net/npm/simplemde@1.11.2/dist/simplemde.min.js\"></script>";
    ss << "<script>var mde;function syncEdit(){document.getElementById('edit-editor').value=mde.value();}document.addEventListener('DOMContentLoaded',function(){mde=new SimpleMDE({element:document.getElementById('edit-editor'),spellChecker:false,status:false,toolbar:['bold','italic','|','heading','|','quote','code','unordered-list','|','link','image','|','preview','side-by-side']});});</script>";
    return ss.str();
}

// 提交博客
string renderSubmit(const string& title, const string& author, const string& content, const string& lang = "zh") {
    Post p;
    p.id = genId();
    p.title = title;
    p.author = author.empty() ? "博主" : sanitizeField(author);
    p.content = sanitizeContent(content);
    p.time = now();
    p.status = "approved";  // 博客直接发布
    p.reply_count = "0";

    ofstream f(POSTS_FILE, ios::app);
    f << serializePost(p) << endl;

    mkdir(DATA_DIR.c_str(), 0755);

    return "<div class=\"success\">" + string(lang == "en" ? "Blog published!" : "博客已发布！") + "</div><div style=\"text-align:center;margin-top:32px;\"><a href=\"/?lang=" + lang + "\" class=\"btn-outline\" style=\"padding:10px 24px;border-radius:6px;text-decoration:none;border:1px solid #1976d2;color:#1976d2;\">" + (lang == "en" ? "< Back to Blog" : "← 返回博客") + "</a></div>";
}


// 审核操作
string doApprove(const string& id, vector<Post>& posts, const string& lang = "zh") {
    for (auto& p : posts) {
        if (p.id == id) {
            p.status = "approved";
            break;
        }
    }
    savePosts(posts);
    string t1 = lang == "en" ? "Approved!" : "审核通过！";
    string t2 = lang == "en" ? "Back to Admin" : "返回管理";
    return "<div class=\"success\"><h3>✅ " + t1 + "</h3><p><a href=\"?a=admin&lang=" + lang + "\" style=\"color:#667eea;\">" + t2 + "</a></p></div>";
}

string doReject(const string& id, vector<Post>& posts, const string& lang = "zh") {
    for (auto& p : posts) {
        if (p.id == id) {
            p.status = "rejected";
            break;
        }
    }
    savePosts(posts);
    string t = lang == "en" ? "Rejected" : "已拒绝";
    string t2 = lang == "en" ? "Back to Admin" : "返回管理";
    return "<div class=\"success\"><h3>❌ " + t + "</h3><p><a href=\"?a=admin&lang=" + lang + "\" style=\"color:#667eea;\">" + t2 + "</a></p></div>";
}

// 博客详情
string renderView(const string& id, const string& lang = "zh") {
    vector<Post> posts = loadPosts();
    Post* pPtr = nullptr;
    for (auto& p : posts) {
        if (p.id == id) {
            pPtr = &p;
            break;
        }
    }
    // 非公开状态(待审核/已拒绝)的博文详情页一律不可见
    if (pPtr && pPtr->status != "approved") pPtr = nullptr;

    bool en = (lang == "en");
    string tNotFound = en ? "Post not found" : "博客不存在";
    string tBlog = en ? "Blog" : "博客";
    string tWrite = en ? "Write" : "写博客";
    string tAdmin = en ? "Admin" : "管理";
    string tComments = en ? "Comments" : "评论";
    string tAddComment = en ? "Add Comment" : "发表评论";
    string tNickname = en ? "Nickname" : "昵称";
    string tNicknamePH = en ? "Your nickname" : "你的昵称";
    string tContent = en ? "Comment" : "评论内容";
    string tContentPH = en ? "Write your comment..." : "写下你的评论...";
    string tMD = en ? "Markdown supported" : "支持 Markdown 格式";
    string tSubmit = en ? "Submit" : "发表评论";
    string tBack = en ? "< Back to Blog" : "← 返回博客";

    if (!pPtr) {
        return "<div class=\"error\">" + tNotFound + "</div>" + renderList(posts, lang);
    }

    stringstream ss;
    ss << "<div class=\"nav\">";
    ss << "<a href=\"/?lang=" << lang << "\">" << tBlog << "</a>";
    ss << "<a href=\"?a=post&lang=" << lang << "\">" << tWrite << "</a>";
    ss << "<a href=\"?a=admin&lang=" << lang << "\">" << tAdmin << "</a>";
    ss << "</div>";
    ss << "<div class=\"post\">";
    ss << "<div class=\"post-title\" style=\"font-size:1.6rem;\">" << escape(pPtr->title) << "</div>";
    ss << "<div class=\"post-meta\">" << pPtr->time << " · " << escape(pPtr->author) << "</div>";
    ss << "<div class=\"post-content md-content\" style=\"padding:24px 0;\">" << md2html(pPtr->content) << "</div>";
    ss << "</div>";

    // 显示已通过的回复
    vector<Reply> replies = loadReplies(id, true);
    if (!replies.empty()) {
        ss << "<div class=\"post\"><h3 style=\"margin-bottom:16px;\">" << tComments << " (" << replies.size() << ")</h3>";
        for (const auto& r : replies) {
            ss << "<div style=\"padding:16px 0;border-bottom:1px solid #eee;\">";
            ss << "<div style=\"font-size:0.85rem;color:#9e9e9e;margin-bottom:8px;\">" << escape(r.author) << " · " << r.time << "</div>";
            ss << "<div class=\"md-content\">" << md2html(r.content) << "</div>";
            ss << "</div>";
        }
        ss << "</div>";
    }

    // 回复表单
    ss << "<div class=\"post\">";
    ss << "<h3 style=\"margin-bottom:16px;\">" << tAddComment << "</h3>";
    ss << "<form action=\"?a=doreply&id=" << id << "&lang=" << lang << "\" method=\"post\" id=\"replyForm\">";
    ss << "<div style=\"margin-bottom:16px;\">";
    ss << "<label>" << tNickname << "</label>";
    ss << "<input name=\"author\" placeholder=\"" << tNicknamePH << "\" required>";
    ss << "</div>";
    ss << "<div style=\"margin-bottom:16px;\">";
    ss << "<label>" << tContent << "</label>";
    ss << "<textarea name=\"content\" id=\"reply-editor\" placeholder=\"" << tContentPH << "\" style=\"min-height:120px;\" required></textarea>";
    ss << "</div>";
    ss << "<div style=\"margin-bottom:12px;font-size:0.8rem;color:#9e9e9e;\">" << tMD << "</div>";
    ss << "<button type=\"submit\" onclick=\"syncReply()\">" << tSubmit << "</button>";
    ss << "</form></div>";
    ss << "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/simplemde@1.11.2/dist/simplemde.min.css\">";
    ss << "<script src=\"https://cdn.jsdelivr.net/npm/simplemde@1.11.2/dist/simplemde.min.js\"></script>";
    ss << "<script>var mde;function syncReply(){document.getElementById('reply-editor').value=mde.value();}document.addEventListener('DOMContentLoaded',function(){mde=new SimpleMDE({element:document.getElementById('reply-editor'),spellChecker:false,status:false,toolbar:['bold','italic','|','heading','|','quote','code','|','link','|','preview']});});</script>";
    ss << "<div style=\"text-align:center;margin-top:32px;\">";
    ss << "<a href=\"/?lang=" << lang << "\">" << tBack << "</a>";
    ss << "</div>";

    return ss.str();
}

// 处理回复
string doReply(const string& id, const string& author, const string& content, const string& lang = "zh") {
    addReply(id, author, content);
    updateReplyCount(id);
    string t = lang == "en" ? "Comment submitted, awaiting approval" : "评论已提交，待审核后显示";
    return "<div class=\"success\">" + t + "</div>" + renderView(id, lang);
}

// 渲染管理面板
string renderAdmin(const vector<Post>& posts, const string& lang = "zh") {
    bool en = (lang == "en");
    string tAdmin = en ? "Admin" : "管理";
    string tAdminPanel = en ? "Blog Admin Panel" : "博客管理面板";
    string tBlog = en ? "Blog" : "博客";
    string tWrite = en ? "Write" : "写博客";
    string tPending = en ? "Pending Comments" : "待审核评论";
    string tNoPending = en ? "No pending comments" : "暂无待审核评论";
    string tApprove = en ? "Approve" : "通过";
    string tDelete = en ? "Delete" : "删除";
    string tDanger = en ? "Danger Zone" : "危险操作";
    string tDelAll = en ? "Delete All Blogs" : "删除全部博客";
    string tDelAllConfirm = en ? "Are you sure you want to delete all blogs? This cannot be undone!" : "确定要删除全部博客吗？此操作不可恢复！";
    string tBlogList = en ? "Blog List" : "博客列表";
    string tNoBlogs = en ? "No blogs yet" : "暂无博客";
    string tEdit = en ? "Edit" : "编辑";
    string tDelBlogConfirm = en ? "Are you sure you want to delete this blog?" : "确定删除该博客？";
    string tDelCommentConfirm = en ? "Are you sure you want to delete this comment?" : "确定删除该评论？";

    // 这些链接均已带 "?a=" 查询串，语言参数必须用 & 连接，不能用 ? 二次起头
    string baseParams = "&lang=" + lang;

    stringstream ss;
    ss << "<div class=\"header\">";
    ss << "<h1>" << tAdmin << "</h1>";
    ss << "<p>" << tAdminPanel << "</p>";
    ss << "</div>";
    ss << "<div class=\"nav\">";
    ss << "<a href=\"/?lang=" << lang << "\">" << tBlog << "</a>";
    ss << "<a href=\"?a=post&lang=" << lang << "\">" << tWrite << "</a>";
    ss << "</div>";

    // 待审核评论
    vector<Reply> pendingReplies = loadAllPendingReplies();
    ss << "<div class=\"post\">";
    ss << "<h3 style=\"margin-bottom:16px;\">" << tPending << " (" << pendingReplies.size() << ")</h3>";
    if (pendingReplies.empty()) {
        ss << "<div class=\"empty\">" << tNoPending << "</div>";
    } else {
        for (const auto& r : pendingReplies) {
            ss << "<div style=\"padding:16px 0;border-bottom:1px solid #eee;\">";
            ss << "<div style=\"font-size:0.85rem;color:#9e9e9e;margin-bottom:8px;\">" << escape(r.author) << " · " << r.time << "</div>";
            ss << "<div class=\"md-content\" style=\"margin-bottom:12px;\">" << md2html(r.content) << "</div>";
            ss << "<a href=\"?a=approvereply&id=" << r.id << baseParams << "\" class=\"btn-small\" style=\"background:#4caf50;color:#fff;padding:6px 16px;border-radius:4px;text-decoration:none;margin-right:8px;\">" << tApprove << "</a>";
            ss << "<a href=\"?a=delreply&id=" << r.id << baseParams << "\" class=\"btn-small\" style=\"background:#f44336;color:#fff;padding:6px 16px;border-radius:4px;text-decoration:none;\" onclick=\"return confirm('" << tDelCommentConfirm << "')\">" << tDelete << "</a>";
            ss << "</div>";
        }
    }
    ss << "</div>";

    // 危险操作
    ss << "<div class=\"post\" style=\"background:#ffebee;border-left:4px solid #d32f2f;\">";
    ss << "<h3 style=\"color:#c62828;margin-bottom:16px;\">" << tDanger << "</h3>";
    ss << "<form action=\"?a=deleteall" << baseParams << "\" method=\"post\" onsubmit=\"return confirm('" << tDelAllConfirm << "')\">";
    ss << "<button type=\"submit\" class=\"btn-danger\">" << tDelAll << "</button>";
    ss << "</form>";
    ss << "</div>";

    // 博客列表
    ss << "<div class=\"post\"><h3 style=\"margin-bottom:20px;\">" << tBlogList << " (" << posts.size() << ")</h3>";
    if (posts.empty()) {
        ss << "<div class=\"empty\">" << tNoBlogs << "</div>";
    } else {
        for (const auto& p : posts) {
            ss << "<div style=\"padding:12px 0;border-bottom:1px solid #eee;\">";
            ss << "<div style=\"font-weight:500;color:#1a1a1a;\">" << escape(p.title) << "</div>";
            ss << "<div style=\"font-size:0.8rem;color:#9e9e9e;margin-top:4px;\">" << p.time << " · " << escape(p.author) << "</div>";
            ss << "<div style=\"margin-top:8px;\">";
            ss << "<a href=\"?a=edit&id=" << p.id << baseParams << "\" class=\"btn-small\" style=\"background:#1976d2;color:#fff;padding:4px 12px;border-radius:4px;text-decoration:none;margin-right:8px;\">" << tEdit << "</a>";
            ss << "<a href=\"?a=delblog&id=" << p.id << baseParams << "\" class=\"btn-small\" style=\"background:#f44336;color:#fff;padding:4px 12px;border-radius:4px;text-decoration:none;\" onclick=\"return confirm('" << tDelBlogConfirm << "')\">" << tDelete << "</a>";
            ss << "</div>";
            ss << "</div>";
        }
    }
    ss << "</div>";

    return ss.str();
}

int main() {
    srand(time(NULL));
    
    // 确保数据目录存在
    mkdir(DATA_DIR.c_str(), 0755);
    
    string qs = env("QUERY_STRING");
    string method = env("REQUEST_METHOD");
    string action = getq(qs, "a");
    string lang = getq(qs, "lang");
    // 语言参数白名单：非法值一律回落默认，防止 lang 拼进HTML属性引发反射型XSS
    if (lang != "zh" && lang != "en") lang = "en";
    if (action.empty()) action = "list";
    
    string langPrefix = (lang == "en") ? "&lang=en" : "";
    
    // 统一鉴权：只认服务端会话Cookie，URL里的auth参数不再作为凭据
    bool authed = isAdminAuthed();
    string denyMsg = (lang == "en") ? "Not authorized, please sign in first"
                                    : "未授权操作，请先登录管理账号";
    
    string result;
    vector<Post> posts = loadPosts();
    
    // 退出登录：销毁服务端会话并清除Cookie
    if (action == "logout") {
        destroySession(getCookieValue("cppblog_sid"));
        clearSessionCookie();
        string tTip = (lang == "en") ? "Signed out" : "已退出登录";
        html("My Blog", lang, "<div class=\"success\">" + tTip + "</div>" + renderList(posts, lang));
        return 0;
    }
    
    if (action == "list") {
        result = renderList(posts, lang);
    }
    else if (action == "post") {
        // 写博客需要管理员会话
        if (authed) {
            result = renderPostForm(lang);
        } else {
            result = renderBlogLoginForm("", "blog", lang);
        }
    }
    else if (action == "bloglogin" && method == "POST") {
        string postData = readPostBody(MAX_BODY);
        string password = getq(postData, "password");
        if (password == ADMIN_PASSWORD) {
            destroySession(getCookieValue("cppblog_sid"));   // 轮换旧会话，防范会话固定
            setSessionCookie(createSession());       // 登录成功，仅下发会话Cookie
            result = renderPostForm(lang);
        } else {
            result = renderBlogLoginForm(lang == "en" ? "Wrong password" : "密码错误", "blog", lang);
        }
    }
    else if (action == "submit" && method == "POST") {
        // 读取POST数据
        string postData = readPostBody(MAX_BODY);
        
        if (!authed) {
            // 发布博客必须鉴权，杜绝任意访客发文
            result = "<div class=\"error\">" + denyMsg + "</div>" + renderBlogLoginForm("", "blog", lang);
        } else {
            string title = sanitizeField(getq(postData, "title"));
            string author = "";
            string content = getq(postData, "content");
            if (!title.empty() && !content.empty()) {
                result = renderSubmit(title, author, content, lang);
            } else {
                result = "<div class=\"error\">" + string(lang == "en" ? "Please fill all fields" : "请填写完整信息") + "</div>" + renderPostForm(lang);
            }
        }
    }
    else if (action == "admin") {
        // 管理面板必须会话鉴权
        if (authed) {
            result = renderAdmin(posts, lang);
        } else {
            result = renderBlogLoginForm("", "admin", lang);
        }
    }
    else if ((action == "adminlogin" || action == "login") && method == "POST") {
        string postData = readPostBody(MAX_BODY);
        string password = getq(postData, "password");
        if (password == ADMIN_PASSWORD) {
            destroySession(getCookieValue("cppblog_sid"));   // 轮换旧会话，防范会话固定
            setSessionCookie(createSession());       // 下发会话Cookie，密码不再出现在URL/页面中
            string tTip = (lang == "en") ? "Sign in successful" : "登录成功";
            result = "<div class=\"success\">" + tTip + "</div>" + renderAdmin(posts, lang);
        } else {
            result = renderBlogLoginForm(lang == "en" ? "Wrong password" : "密码错误", "admin", lang);
        }
    }
    else if (action == "view") {
        string id = normalizeId(getq(qs, "id"));
        result = renderView(id, lang);
    }
    else if (action == "doreply" && method == "POST") {
        string id = normalizeId(getq(qs, "id"));
        string postData = readPostBody(MAX_BODY);
        string author = getq(postData, "author");
        string content = getq(postData, "content");
        result = doReply(id, author, content, lang);
    }
    else if (action == "approvereply") {
        string id = normalizeId(getq(qs, "id"));
        string tTip = (lang == "en") ? "Comment approved" : "评论已通过审核";
        if (authed) {
            approveReply(id);
            result = "<div class=\"success\">" + tTip + "</div>" + renderAdmin(loadPosts(), lang);
        } else {
            result = "<div class=\"error\">" + denyMsg + "</div>" + renderBlogLoginForm("", "admin", lang);
        }
    }
    else if (action == "delreply") {
        string id = normalizeId(getq(qs, "id"));
        string tTip = (lang == "en") ? "Comment deleted" : "评论已删除";
        if (authed) {
            deleteReply(id);
            result = "<div class=\"success\">" + tTip + "</div>" + renderAdmin(loadPosts(), lang);
        } else {
            result = "<div class=\"error\">" + denyMsg + "</div>" + renderBlogLoginForm("", "admin", lang);
        }
    }
    else if (action == "edit") {
        string id = normalizeId(getq(qs, "id"));
        if (authed) {
            result = renderEditForm(id, lang);
        } else {
            result = "<div class=\"error\">" + denyMsg + "</div>" + renderBlogLoginForm("", "admin", lang);
        }
    }
    else if (action == "update" && method == "POST") {
        string postData = readPostBody(MAX_BODY);
        if (authed) {
            string id = normalizeId(getq(postData, "id"));
            string title = sanitizeField(getq(postData, "title"));
            string content = getq(postData, "content");
            updatePost(id, title, content);
            result = "<div class=\"success\">" + string(lang == "en" ? "Blog updated!" : "博客已更新！") + "</div><div style=\"text-align:center;margin-top:20px;\"><a href=\"/?lang=" + lang + "\">" + (lang == "en" ? "< Back to Blog" : "← 返回博客") + "</a></div>";
        } else {
            result = "<div class=\"error\">" + denyMsg + "</div>" + renderBlogLoginForm("", "admin", lang);
        }
    }
    else if (action == "delblog") {
        string id = normalizeId(getq(qs, "id"));
        string tTip = (lang == "en") ? "Blog deleted" : "博客已删除";
        if (authed) {
            deleteBlog(id);
            posts = loadPosts();
            result = "<div class=\"success\">" + tTip + "</div>" + renderAdmin(posts, lang);
        } else {
            result = "<div class=\"error\">" + denyMsg + "</div>" + renderBlogLoginForm("", "admin", lang);
        }
    }
    else if (action == "deleteall" && method == "POST") {
        // 清库：严格会话鉴权
        if (authed) {
            deleteAllPosts();
            posts = loadPosts();
            string tTip = (lang == "en") ? "All blogs deleted" : "所有博客已删除";
            result = "<div class=\"success\">" + tTip + "</div>" + renderAdmin(posts, lang);
        } else {
            result = "<div class=\"error\">" + denyMsg + "</div>" + renderBlogLoginForm("", "admin", lang);
        }
    }
    else {
        result = renderList(posts, lang);
    }
    
    html("My Blog", lang, result);
    return 0;
}
