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
#include <sys/stat.h>

using namespace std;

const string DATA_DIR = "/usr/lib/cppblog/";
const string POSTS_FILE = DATA_DIR + "/cppblog.md";
const string ADMIN_PASSWORD = "YOUR_ADMIN_PASSWORD_HERE";  // TODO: 部署前替换为强密码，勿提交真实密码到版本库

// 获取环境变量
const char* env(const char* name) {
    return getenv(name) ? getenv(name) : "";
}

// URL解码
string decode(const string& s) {
    string r;
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '%' && i + 2 < s.length()) {
            int v;
            sscanf(s.c_str() + i + 1, "%2x", &v);
            r += char(v);
            i += 2;
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
    
    // 链接 [text](url)
    pos = 0;
    while ((pos = s.find("[", pos)) != string::npos) {
        size_t end = s.find("](", pos);
        size_t urlEnd = s.find(")", end + 2);
        if (end == string::npos || urlEnd == string::npos) break;
        string text = s.substr(pos + 1, end - pos - 1);
        string url = s.substr(end + 2, urlEnd - end - 2);
        s.replace(pos, urlEnd - pos + 1, "<a href=\"" + url + "\" target=\"_blank\">" + text + "</a>");
        pos = urlEnd + 1;
    }
    
    // 图片 ![alt](url)
    pos = 0;
    while ((pos = s.find("![", pos)) != string::npos) {
        size_t end = s.find("](", pos);
        size_t urlEnd = s.find(")", end + 2);
        if (end == string::npos || urlEnd == string::npos) break;
        string alt = s.substr(pos + 2, end - pos - 2);
        string url = s.substr(end + 2, urlEnd - end - 2);
        s.replace(pos, urlEnd - pos + 1, "<img src=\"" + url + "\" alt=\"" + alt + "\" style=\"max-width:100%;\">");
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

// 解析参数
string getq(const string& qs, const string& key) {
    string d = decode(qs);
    string k = key + "=";
    size_t p = d.find(k);
    if (p == string::npos) {
        k = "&" + key + "=";
        p = d.find(k);
        if (p == string::npos) return "";
        p++;
    }
    p += key.length() + 1;
    size_t e = d.find('&', p);
    if (e == string::npos) e = d.length();
    return d.substr(p, e - p);
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
    string replyFile = DATA_DIR + "/reply_" + postId + ".txt";
    ofstream f(replyFile, ios::app);
    f << genId() << "|" << author << "|" << content << "|" << now() << "|pending|" << endl;
}

// 加载回复 - 参数: postId, showPending(是否显示待审核)
struct Reply {
    string id, author, content, time, status;
};

vector<Reply> loadReplies(const string& postId, bool approvedOnly = true) {
    vector<Reply> replies;
    string replyFile = DATA_DIR + "/reply_" + postId + ".txt";
    ifstream f(replyFile);
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

// 加载所有待审核的回复
vector<Reply> loadAllPendingReplies() {
    vector<Reply> allReplies;
    string cmd = "ls " + DATA_DIR + "/reply_*.txt 2>/dev/null";
    FILE* fp = popen(cmd.c_str(), "r");
    if (fp) {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            string f = buf;
            f = f.substr(0, f.find('\n'));
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
        pclose(fp);
    }
    return allReplies;
}

// 审核回复
void approveReply(const string& replyFullId) {
    size_t sep = replyFullId.find('_');
    if (sep == string::npos) return;
    string postId = replyFullId.substr(0, sep);
    string replyId = replyFullId.substr(sep + 1);
    string replyFile = DATA_DIR + "/reply_" + postId + ".txt";
    
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
    string replyFile = DATA_DIR + "/reply_" + postId + ".txt";
    
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
    vector<Post> posts = loadPosts();
    for (auto& p : posts) {
        if (p.id == id) {
            p.title = title;
            p.content = content;
            break;
        }
    }
    savePosts(posts);
}

// 删除博客
void deleteBlog(const string& id) {
    vector<Post> posts = loadPosts();
    vector<Post> newPosts;
    for (auto& p : posts) {
        if (p.id != id) {
            newPosts.push_back(p);
        }
    }
    savePosts(newPosts);
    // 删除该博客的回复文件
    string cmd = "rm -f " + DATA_DIR + "/reply_" + id + ".txt";
    system(cmd.c_str());
}

void deleteAllPosts() {
    // 清空所有帖子
    ofstream f(POSTS_FILE, ios::trunc);
    f.close();
    // 删除所有回复文件
    string cmd = "rm -f " + DATA_DIR + "/reply_*.txt";
    system(cmd.c_str());
}

// HTML页面 - Material-UI浅色极简风格
void html(const string& title, const string& lang, const string& content) {
    string langSwitch = (lang == "en") ? "<a href=\"?lang=zh\" style=\"position:fixed;top:16px;right:20px;padding:6px 12px;background:#fff;border-radius:4px;text-decoration:none;color:#1976d2;font-size:0.85rem;box-shadow:0 1px 3px rgba(0,0,0,0.1);\">中文</a>" : "<a href=\"?lang=en\" style=\"position:fixed;top:16px;right:20px;padding:6px 12px;background:#fff;border-radius:4px;text-decoration:none;color:#1976d2;font-size:0.85rem;box-shadow:0 1px 3px rgba(0,0,0,0.1);\">English</a>";
    cout << "Content-type: text/html; charset=utf-8\r\n\r\n";
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
        ss << "<div class=\"post-content\">" << md2html(p.content).substr(0, 300);
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
    ss << "<input type=\"hidden\" name=\"id\" value=\"" << id << "\">";
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
    p.author = author.empty() ? "博主" : author;
    p.content = content;
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
string renderAdmin(const vector<Post>& posts, const string& lang = "zh", const string& auth = "") {
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

    string authParam = auth.empty() ? "" : "&auth=" + auth;
    string baseParams = "?lang=" + lang + authParam;

    stringstream ss;
    ss << "<div class=\"header\">";
    ss << "<h1>" << tAdmin << "</h1>";
    ss << "<p>" << tAdminPanel << "</p>";
    ss << "</div>";
    ss << "<div class=\"nav\">";
    ss << "<a href=\"/?lang=" << lang << authParam << "\">" << tBlog << "</a>";
    ss << "<a href=\"?a=post&lang=" << lang << authParam << "\">" << tWrite << "</a>";
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
    if (lang.empty()) lang = "en";  // 默认英文
    if (action.empty()) action = "list";
    
    string langPrefix = (lang == "en") ? "&lang=en" : "";
    
    // 获取auth参数
    string auth = getq(qs, "auth");
    
    string result;
    vector<Post> posts = loadPosts();
    
    if (action == "list") {
        result = renderList(posts, lang);
    }
    else if (action == "post") {
        // 检查是否已验证博客密码
        string auth = getq(qs, "auth");
        if (auth == ADMIN_PASSWORD) {
            result = renderPostForm(lang);
        } else {
            result = renderBlogLoginForm("", "blog", lang);
        }
    }
    else if (action == "bloglogin" && method == "POST") {
        string postData;
        char c;
        while (cin.get(c)) postData += c;
        string password = getq(postData, "password");
        if (password == ADMIN_PASSWORD) {
            result = renderPostForm(lang);
        } else {
            result = renderBlogLoginForm(lang == "en" ? "Wrong password" : "密码错误", "blog", lang);
        }
    }
    else if (action == "submit" && method == "POST") {
        // 读取POST数据
        string postData;
        char c;
        while (cin.get(c)) postData += c;
        
        string title = getq(postData, "title");
        string author = "";
        string content = getq(postData, "content");
        
        if (!title.empty() && !content.empty()) {
            result = renderSubmit(title, author, content, lang);
        } else {
            result = "<div class=\"error\">" + string(lang == "en" ? "Please fill all fields" : "请填写完整信息") + "</div>" + renderPostForm(lang);
        }
    }
    else if (action == "admin") {
        // 检查是否已验证
        string storedAuth = "";
        const char* envAuth = env("HTTP_COOKIE");
        if (envAuth) {
            string cookie = envAuth;
            size_t p = cookie.find("admin_auth=");
            if (p != string::npos) {
                storedAuth = cookie.substr(p + 11, 32);
            }
        }
        
        // 简单验证：通过URL参数传递
        if (auth == ADMIN_PASSWORD || storedAuth == ADMIN_PASSWORD) {
            result = renderAdmin(posts, lang, auth);
        } else {
            result = renderBlogLoginForm("", "admin", lang);
        }
    }
    else if (action == "adminlogin" && method == "POST") {
        string postData;
        char c;
        while (cin.get(c)) postData += c;
        string password = getq(postData, "password");
        if (password == ADMIN_PASSWORD) {
            result = "<div class=\"success\">" + string(lang == "en" ? "Login success" : "登录成功") + "</div>" + renderAdmin(posts, lang, "verified");
        } else {
            result = renderBlogLoginForm(lang == "en" ? "Wrong password" : "密码错误", "admin", lang);
        }
    }
    else if (action == "login" && method == "POST") {
        string postData;
        char c;
        while (cin.get(c)) postData += c;
        string password = getq(postData, "password");
        if (password == ADMIN_PASSWORD) {
            result = "<div class=\"success\">" + string(lang == "en" ? "Login success" : "登录成功") + "</div>" + renderAdmin(posts, lang, "verified");
        } else {
            result = renderBlogLoginForm(lang == "en" ? "Wrong password" : "密码错误", "admin", lang);
        }
    }
    else if (action == "view") {
        string id = getq(qs, "id");
        result = renderView(id, lang);
    }
    else if (action == "doreply" && method == "POST") {
        string id = getq(qs, "id");
        string postData;
        char c;
        while (cin.get(c)) postData += c;
        string author = getq(postData, "author");
        string content = getq(postData, "content");
        result = doReply(id, author, content, lang);
    }
    else if (action == "approvereply") {
        string id = getq(qs, "id");
        approveReply(id);
        result = "<div class=\"success\">" + string(lang == "en" ? "Comment approved" : "评论已通过审核") + "</div>" + renderAdmin(posts, lang, auth);
    }
    else if (action == "delreply") {
        string id = getq(qs, "id");
        deleteReply(id);
        result = "<div class=\"success\">" + string(lang == "en" ? "Comment deleted" : "评论已删除") + "</div>" + renderAdmin(posts, lang, auth);
    }
    else if (action == "edit") {
        string id = getq(qs, "id");
        result = renderEditForm(id, lang);
    }
    else if (action == "update" && method == "POST") {
        string postData;
        char c;
        while (cin.get(c)) postData += c;
        string id = getq(postData, "id");
        string title = getq(postData, "title");
        string content = getq(postData, "content");
        updatePost(id, title, content);
        result = "<div class=\"success\">" + string(lang == "en" ? "Blog updated!" : "博客已更新！") + "</div><div style=\"text-align:center;margin-top:20px;\"><a href=\"/?lang=" + lang + "\">" + (lang == "en" ? "< Back to Blog" : "← 返回博客") + "</a></div>";
    }
    else if (action == "delblog") {
        string id = getq(qs, "id");
        deleteBlog(id);
        posts = loadPosts();
        result = "<div class=\"success\">" + string(lang == "en" ? "Blog deleted" : "博客已删除") + "</div>" + renderAdmin(posts, lang, auth);
    }
    else if (action == "deleteall" && method == "POST") {
        // 验证权限
        string storedAuth = "";
        const char* envAuth = env("HTTP_COOKIE");
        if (envAuth) {
            string cookie = envAuth;
            size_t p = cookie.find("admin_auth=");
            if (p != string::npos) {
                storedAuth = cookie.substr(p + 11, 32);
            }
        }
        if (auth == ADMIN_PASSWORD || storedAuth == ADMIN_PASSWORD) {
            deleteAllPosts();
            posts = loadPosts();
            result = "<div class=\"success\">" + string(lang == "en" ? "All blogs deleted" : "所有博客已删除") + "</div>" + renderAdmin(posts, lang, auth);
        } else {
            result = renderBlogLoginForm(lang == "en" ? "Unauthorized" : "未授权", "admin", lang);
        }
    }
    else {
        result = renderList(posts, lang);
    }
    
    html("My Blog", lang, result);
    return 0;
}
