# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] — 2026-05-22

### Added

- **Core blog engine** — single-file C++ CGI implementation with zero external dependencies
- **File-based storage** — pipe-delimited plaintext format for posts and comments
- **Markdown renderer** — built-in support for bold, italic, headings (H1–H4), inline code, code blocks, links, images, unordered lists, and horizontal rules
- **Bilingual interface** — Chinese / English switching via `?lang=zh` / `?lang=en` query parameter
- **Post management** — create, edit, delete, and view blog posts
- **Comment system** — visitor commenting with pending/approved/rejected moderation workflow
- **Admin panel** — password-protected panel for approving/deleting comments, editing/deleting posts, and bulk-deleting all content
- **Material-inspired UI** — clean, responsive design using CSS with Noto Sans SC font
- **SimpleMDE integration** — in-browser Markdown editor for writing and editing posts/comments

### Security

- Removed hardcoded admin password from source; replaced with placeholder `YOUR_ADMIN_PASSWORD_HERE`
- Removed admin password from URL redirect parameters after login (`doApprove`, `doReject`)
- Login handlers now pass `"verified"` token instead of the raw password constant to `renderAdmin()`

---

## [Unreleased]

### Planned

- Environment variable support for `ADMIN_PASSWORD` (avoid storing credentials in source)
- CSRF token protection for all state-changing forms
- Session cookie-based authentication (replace URL `auth` parameter)
- Input length validation for post title and content
- Configurable data directory via environment variable

---

<!-- Links -->
[1.0.0]: https://github.com/YOUR_USERNAME/cppblog/releases/tag/v1.0.0
[Unreleased]: https://github.com/YOUR_USERNAME/cppblog/compare/v1.0.0...HEAD
