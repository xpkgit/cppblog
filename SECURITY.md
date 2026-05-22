# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| latest (`main`) | ✅ Yes |

---

## ⚠️ Known Security Considerations

cppblog is a **single-binary CGI program** designed for personal/hobbyist use. Before deploying to any public-facing server, please review and address the following:

### 1. Admin Password (Critical)

The admin password is stored as a plain-text constant in the source code.

**Before compiling:**

```cpp
// cppblog.cpp — change this line
const string ADMIN_PASSWORD = "YOUR_ADMIN_PASSWORD_HERE";
```

**Recommendations:**
- Use a strong, randomly generated password (16+ characters, mixed case, numbers, symbols)
- Never commit your real password to version control
- Consider loading the password from an environment variable instead:

```cpp
const char* _pwd = getenv("CPPBLOG_ADMIN_PWD");
const string ADMIN_PASSWORD = _pwd ? string(_pwd) : "";
```

Then set the environment variable in your CGI wrapper or Apache/Nginx config:

```apache
# Apache: /etc/apache2/envvars or .htaccess
SetEnv CPPBLOG_ADMIN_PWD "your-strong-password"
```

### 2. URL-based Authentication

The current admin authentication passes a verification token via URL query parameters (`?auth=verified`). This means:

- The token appears in server access logs
- It may be cached by browsers or proxies

**Recommendation:** For production use, replace URL-based auth with an HTTP-only session cookie mechanism.

### 3. No CSRF Protection

Form submissions are not protected against Cross-Site Request Forgery (CSRF). An attacker could trick a logged-in admin into performing unwanted actions.

**Recommendation:** Add a per-session CSRF token to all state-changing forms.

### 4. No Input Length Limits

Post titles and content are not length-limited server-side. Very large inputs could degrade performance.

**Recommendation:** Add length checks before writing to files.

### 5. Command Injection Risk (`popen` / `system`)

The code uses `popen()` and `system()` with paths constructed from `DATA_DIR`. As long as `DATA_DIR` is a hard-coded constant (not user input), this is safe. However:

- Ensure `DATA_DIR` is never derived from user-controlled input
- If you modify the code to accept configurable paths, sanitize thoroughly

### 6. Data Directory Permissions

The data directory must be writable by the web server process (`www-data`), but should not be directly accessible via HTTP.

```bash
# Recommended: place data outside web root
sudo mkdir -p /var/lib/cppblog/
sudo chown www-data:www-data /var/lib/cppblog/
sudo chmod 700 /var/lib/cppblog/
```

Update `DATA_DIR` in `cppblog.cpp` accordingly.

### 7. Prevent Source Code Exposure

Do not place `cppblog.cpp` or data files inside the web root. Only the compiled `.cgi` binary should be accessible via the web server.

---

## 🔐 Recommended Production Checklist

Before going live:

- [ ] Changed `ADMIN_PASSWORD` to a strong, unique password
- [ ] Data directory is outside the web root
- [ ] `.cpp` source file is not served by the web server
- [ ] HTTPS is enabled (via Let's Encrypt / Certbot)
- [ ] Data files are excluded from version control (`.gitignore`)
- [ ] Regular backups of the data directory are in place

---

## Reporting a Vulnerability

If you discover a security vulnerability, please open a **private** GitHub Security Advisory or contact the maintainer directly via the email listed on their GitHub profile.

Please do **not** open a public issue for security vulnerabilities.
