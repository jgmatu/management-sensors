# Security Testing Plan

This document defines the security testing strategy for the Management Sensors
platform. It identifies the system's attack surface, threat boundaries, and the
specific test vectors that must be executed before any production deployment.

---

## System Threat Model

### Trust Boundaries

The system has three trust boundaries. Components inside a boundary share
implicit trust; traffic crossing a boundary must be authenticated, encrypted,
and validated.

```
 ┌──────────────────────────────────┐
 │        EXTERNAL CLIENTS          │
 │                                  │
 │  ┌───────────┐   ┌───────────┐  │
 │  │  Browser  │   │ botan CLI │  │
 │  │  / curl   │   │  client   │  │
 │  └─────┬─────┘   └─────┬─────┘  │
 │        │               │         │
 │  ┌─────┴─────┐         │         │
 │  │   Proxy   │         │         │
 │  │ HTTP:8443 │         │         │
 │  │  -> TLS   │         │         │
 │  └─────┬─────┘         │         │
 └────────┼───────────────┼─────────┘
          │ TLS PQC       │ TLS PQC
          │ :50444        │ :50443
 ═════════╪═══════════════╪══════════════════════════════════════
 ┌────────┼───────────────┼─────────────────────────────────────┐
 │        │  HARDWARE-PROTECTED ZONE                            │
 │  ┌─────┴───────────────┴─────┐                               │
 │  │         Server            │                               │
 │  │  ┌─────────────────────┐  │                               │
 │  │  │QuantumSafeTlsEngine │  │                               │
 │  │  │(Botan TLS v1.3 PQC) │  │                               │
 │  │  └──┬──────────────┬───┘  │                               │
 │  │  ┌──┴──────┐ ┌─────┴───┐ │                               │
 │  │  │  HTTP   │ │   CLI   │ │                               │
 │  │  │ :50444  │ │ :50443  │ │                               │
 │  │  └────┬────┘ └────┬────┘ │                               │
 │  │       └──────┬─────┘     │                               │
 │  │         Dispatcher       │                               │
 │  └────────────┬─────────────┘                               │
 │               │                                              │
 │  ┌────────────┴─────────────┐    ┌────────────────────────┐ │
 │  │       PostgreSQL         ├───►│      Controller        │ │
 │  │    (LISTEN / NOTIFY)     │◄───┤                        │ │
 │  └──────────────────────────┘    └───────────┬────────────┘ │
 │                                  ┌───────────┴────────────┐ │
 │                                  │     MQTT Broker        │ │
 │                                  │     (Mosquitto)        │ │
 │                                  │      :1883             │ │
 │                                  └───────────┬────────────┘ │
 │                                  ┌───────────┴────────────┐ │
 │                                  │    Broker Proxy        │ │
 │                                  │  (Botan TLS v1.3 PQC)  │ │
 │                                  └───────────┬────────────┘ │
 └──────────────────────────────────────────────┼──────────────┘
                                                │
                                       TLS v1.3 PQC tunnel
                                       (Botan, cert-based)
                                                │
 ┌──────────────────────────────────────────────┼──────────────┐
 │             FIELD / SENSOR ZONE              │              │
 │                                  ┌───────────┴────────────┐ │
 │                                  │     Sensor Proxy       │ │
 │                                  │  (Botan TLS v1.3 PQC)  │ │
 │                                  └───────────┬────────────┘ │
 │                                  ┌───────────┴────────────┐ │
 │                                  │     Sensor Node        │ │
 │                                  └────────────────────────┘ │
 └─────────────────────────────────────────────────────────────┘
```

### Exposed Attack Surfaces

| Surface | Port | Protocol | Exposure |
|---|---|---|---|
| **Server (raw mode)** | 50443 | TLS v1.3 PQC | External clients (CLI) |
| **Server (HTTP mode)** | 50444 | TLS v1.3 PQC + HTTP/1.1 | External clients (REST API, browser) |
| **HTTP Proxy** | 8443 | Plain HTTP (loopback only) | Local clients bridging to TLS PQC |
| **Frontend (SPA)** | 3000 | HTTP | Browser users |
| **MQTT Broker** | 1883 | MQTT (plaintext) | Controller + Broker Proxy (internal only) |
| **Broker Proxy** | varies | Botan TLS v1.3 PQC | Tunnel endpoint (hardware zone -> field zone) |
| **Sensor Proxy** | varies | Botan TLS v1.3 PQC | Tunnel endpoint (field zone -> hardware zone) |

### Hardware-Protected (Not Directly Exposed)

| Component | Reason |
|---|---|
| PostgreSQL | Bound to localhost or internal network; no external port exposure |
| Controller | Internal process; communicates only with local DB and MQTT broker |
| MQTT Broker | Bound to internal interface; sensor traffic exits only through Broker Proxy |
| Broker Proxy | Internal-facing Botan proxy; bridges MQTT broker to field zone via TLS v1.3 PQC tunnel |

---

## 1. Web Application Security (Frontend)

### 1.1 Cross-Site Scripting (XSS)

| ID | Test | Vector | Target |
|---|---|---|---|
| XSS-01 | Reflected XSS via sensor hostname | Inject `<script>alert(1)</script>` as hostname in POST /api/config_ip | Server API + frontend table rendering |
| XSS-02 | Stored XSS via sensor IP field | Inject `<img src=x onerror=alert(1)>` as IP value | Database -> frontend display pipeline |
| XSS-03 | DOM-based XSS in SPA routing | Manipulate URL hash/params with JS payloads | Next.js client-side router |
| XSS-04 | XSS via MQTT telemetry payload | Inject script tags in temperature or sensor_id fields published via MQTT | Controller -> DB -> frontend telemetry display |
| XSS-05 | Content-Type sniffing | Send responses without `X-Content-Type-Options: nosniff` | Server HTTP responses |

**Tools**: OWASP ZAP, Burp Suite, manual payload injection.

**Remediation checklist**:
- [ ] All user-supplied data escaped before DOM insertion
- [ ] Content Security Policy (CSP) headers configured
- [ ] `X-Content-Type-Options: nosniff` on all HTTP responses
- [ ] `X-Frame-Options: DENY` to prevent clickjacking
- [ ] Input length limits enforced at API level

### 1.2 Cross-Site Request Forgery (CSRF)

| ID | Test | Vector |
|---|---|---|
| CSRF-01 | Forge POST /api/config_ip from external origin | Craft malicious HTML page that auto-submits config change |
| CSRF-02 | Verify Origin/Referer header validation | Send requests with spoofed or missing Origin |

**Remediation checklist**:
- [ ] CSRF tokens on state-changing endpoints
- [ ] SameSite cookie attribute set to `Strict`
- [ ] Origin header validation in QuantumSafeHttp

---

## 2. SQL Injection

### 2.1 Database Layer Attack Vectors

| ID | Test | Vector | Target |
|---|---|---|---|
| SQLI-01 | Injection via sensor_id parameter | `{"sensor_id":"1; DROP TABLE sensor_config;--","ip":"1.2.3.4/24"}` | POST /api/config_ip |
| SQLI-02 | Injection via IP field | `{"sensor_id":1,"ip":"'; DELETE FROM sensor_state;--"}` | POST /api/config_ip -> add_pending_config() |
| SQLI-03 | Injection via hostname | CLI command `CONFIG_IP 1 ip "'; DROP TABLE sensor_config_pending;--"` | QuantumCommandCli -> add_pending_config() |
| SQLI-04 | Second-order injection | Store payload in sensor_config_pending, trigger on NOTIFY -> controller -> upsert | Full pipeline traversal |
| SQLI-05 | Injection via MQTT payload | Publish crafted JSON to `config/events` with SQL in hostname field | Controller -> upsert_sensor_config() |

**Audit checklist**:
- [ ] All DatabaseManager queries use parameterized statements (pqxx::work)
- [ ] No string concatenation in SQL construction
- [ ] Input validation at API boundary (type, length, format)
- [ ] Database user has minimal required privileges (no DROP, no ALTER)
- [ ] MQTT payload validation before database write

### 2.2 PostgreSQL-Specific

| ID | Test | Description |
|---|---|---|
| PG-01 | NOTIFY payload injection | Inject SQL via pg_notify payload content |
| PG-02 | Sequence manipulation | Attempt to reset request_id_seq via injection |
| PG-03 | Privilege escalation | Verify DB user cannot execute DDL or access pg_catalog |

---

## 3. CVE Scanning and Dependency Audit

### 3.1 C++ Dependencies

| Library | Action |
|---|---|
| Botan 3.x | Check NVD for CVEs against installed version |
| Boost 1.90+ | Check for known vulnerabilities in Beast, JSON, Asio |
| libpqxx 7.x | Audit for SQL injection bypass or connection handling flaws |
| libpq (PostgreSQL) | Verify against PostgreSQL security advisories |
| Paho MQTT C/C++ | Check for buffer overflow or auth bypass CVEs |

**Tools**: `cve-bin-tool`, `grype`, manual NVD search.

### 3.2 Frontend Dependencies

| Library | Action |
|---|---|
| Next.js 16.x | Check Vercel security advisories |
| React 19.x | Check for XSS-related CVEs |
| @headlessui/react | Audit for accessibility bypass issues |
| framer-motion | Check for prototype pollution or injection |

**Tools**: `pnpm audit`, `npm audit`, Snyk, Dependabot.

### 3.3 Infrastructure

| Component | Action |
|---|---|
| PostgreSQL 18.x | Check PGDG security advisories |
| Mosquitto | Check for auth bypass, ACL escalation CVEs |
| RHEL 10 kernel | Verify kernel and glibc against known CVEs |
| OpenSSL / GnuTLS | Ensure no fallback to vulnerable TLS versions |

---

## 4. Network Attack Vectors

### 4.1 Server Port (50443 / 50444)

| ID | Test | Description |
|---|---|---|
| NET-01 | TLS downgrade attack | Attempt TLS 1.2 / 1.1 / 1.0 connection against PQC server |
| NET-02 | Cipher suite manipulation | Request weak cipher suites and verify rejection |
| NET-03 | Certificate chain validation | Present self-signed, expired, or wrong-CN certificates |
| NET-04 | Large payload DoS | Send oversized HTTP body to trigger memory exhaustion |
| NET-05 | Slowloris attack | Open connections with slow header delivery |
| NET-06 | Request smuggling | Send ambiguous Content-Length / Transfer-Encoding |
| NET-07 | Path traversal | Request `GET /api/../../etc/passwd` via HTTP |
| NET-08 | Unauthorized endpoint access | Access API without valid TLS client certificate |
| NET-09 | Replay attack | Capture and replay a valid CONFIG_IP request |
| NET-10 | PQC key exchange validation | Verify ML-KEM / Kyber negotiation is enforced |

**Tools**: `nmap --script ssl-enum-ciphers`, `testssl.sh`, `botan tls_client`,
custom Scapy scripts, Wireshark capture analysis.

### 4.2 MQTT Broker Port (1883)

| ID | Test | Description |
|---|---|---|
| MQTT-01 | Anonymous connection | Connect to broker without credentials |
| MQTT-02 | Topic hijacking | Subscribe to `config/requested` from unauthorized client |
| MQTT-03 | Message injection | Publish fake `config/events` to trigger unauthorized config changes |
| MQTT-04 | Telemetry spoofing | Publish fake `telemetry/state` with arbitrary sensor_id |
| MQTT-05 | Denial of service | Flood broker with high-frequency publishes |
| MQTT-06 | Retained message poisoning | Set retained messages on sensitive topics |
| MQTT-07 | Client ID collision | Connect with same client ID as controller or sensor |

**Remediation checklist**:
- [ ] MQTT broker requires authentication (username/password or certificate)
- [ ] ACLs restrict topic access per client identity
- [ ] Broker bound to internal interface only (not 0.0.0.0)
- [ ] Rate limiting on publish frequency
- [ ] TLS encryption on MQTT transport (port 8883)

### 4.3 Sensor Network Boundary

| ID | Test | Description |
|---|---|---|
| SENS-01 | Rogue sensor connection | Attempt MQTT connection without valid client certificate |
| SENS-02 | Man-in-the-middle | Intercept sensor <-> controller tunnel and attempt downgrade |
| SENS-03 | Certificate revocation | Use a revoked certificate and verify rejection |
| SENS-04 | Replay sensor telemetry | Capture and replay a valid telemetry MQTT message |
| SENS-05 | Sensor impersonation | Present valid cert but claim different sensor_id |
| SENS-06 | Tunnel proxy bypass | Attempt direct connection to MQTT broker skipping TLS tunnel |

---

## 5. Authentication and Authorization

### 5.1 TLS Mutual Authentication (mTLS)

| ID | Test | Description |
|---|---|---|
| AUTH-01 | Server without client cert | Connect to server port without presenting client certificate |
| AUTH-02 | Client with wrong CA | Present certificate signed by untrusted CA |
| AUTH-03 | Expired certificate | Use expired server or client certificate |
| AUTH-04 | Certificate key mismatch | Present certificate with mismatched private key |
| AUTH-05 | CN/SAN validation | Present certificate with wrong CN/SAN for the host |

### 5.2 Sensor Certificate Authentication

| ID | Test | Description |
|---|---|---|
| SCERT-01 | Sensor cert provisioning | Verify each sensor has unique certificate tied to sensor_id |
| SCERT-02 | Certificate rotation | Test re-keying without service interruption |
| SCERT-03 | CRL/OCSP enforcement | Verify revoked sensor certs are rejected in production mode |
| SCERT-04 | Private key protection | Ensure sensor private keys are not extractable |

---

## 6. Data Integrity and Pipeline Security

### 6.1 Request Pipeline Integrity

| ID | Test | Description |
|---|---|---|
| PIPE-01 | Request ID tampering | Modify request_id in NOTIFY payload mid-pipeline |
| PIPE-02 | Dispatcher race condition | Send concurrent requests to exhaust pending_requests (MAX_PENDING=500) |
| PIPE-03 | Timeout exploitation | Trigger intentional timeouts to accumulate stale state |
| PIPE-04 | Response status forgery | Inject fake dispatch(id, SUCCESS) from unauthorized source |

### 6.2 Database Trigger Security

| ID | Test | Description |
|---|---|---|
| TRIG-01 | Trigger bypass | Attempt direct INSERT bypassing trigger notification |
| TRIG-02 | NOTIFY payload size | Send oversized NOTIFY payload to test parser robustness |
| TRIG-03 | Channel injection | Execute `NOTIFY config_events, '<malicious payload>'` directly |

---

## 7. Logging and Monitoring

| ID | Check | Description |
|---|---|---|
| LOG-01 | Sensitive data in logs | Verify no passwords, keys, or tokens appear in log files |
| LOG-02 | Log injection | Inject newlines and control characters via API input |
| LOG-03 | Log file permissions | Verify `logs/*.log` are not world-readable |
| LOG-04 | Audit trail completeness | Every config change must produce a traceable log entry |

---

## 8. Test Execution Framework

### Automated Security Scans

```bash
# CVE scan on C++ binaries
cve-bin-tool ./build/

# Frontend dependency audit
cd frontend && pnpm audit

# TLS configuration scan
testssl.sh --full localhost:50443
testssl.sh --full localhost:50444

# OWASP ZAP baseline scan (against proxy endpoint)
zap-baseline.py -t http://127.0.0.1:8443

# MQTT security scan
mqtt-pwn  # or manual nmap scripts
nmap -sV -p 1883 --script mqtt-subscribe localhost
```

### Manual Penetration Testing

Each test vector in sections 1-6 should be executed manually and documented
with:

1. **Pre-conditions**: System state before test
2. **Steps**: Exact commands or tool configuration
3. **Expected result**: What a secure system should do
4. **Actual result**: What happened
5. **Evidence**: Screenshots, pcap files, log excerpts
6. **Severity**: Critical / High / Medium / Low / Informational
7. **Remediation**: Fix applied or ticket reference

### Test Reports

Store all test results under `security/reports/` with naming convention:

```
security/reports/
  YYYY-MM-DD_<test-category>_report.md
  YYYY-MM-DD_<test-category>_evidence/
```

---

## 9. User Authentication and Database Access Policies

### 9.1 User Authentication

The system must enforce identity verification for every external access point.

| ID | Requirement | Description |
|---|---|---|
| UAUTH-01 | Frontend login | Users must authenticate before accessing the SPA dashboard. Session tokens (JWT or opaque) with expiration and refresh. |
| UAUTH-02 | API authentication | Every request to `/api/*` must carry a valid bearer token or be rejected with 401. |
| UAUTH-03 | CLI authentication | TLS client certificate required for raw mode connections. Certificate CN maps to a user identity. |
| UAUTH-04 | Password policy | Minimum 12 characters, complexity requirements, bcrypt/argon2 hashing. No plaintext storage. |
| UAUTH-05 | Brute-force protection | Lock account after 5 failed attempts. Exponential backoff on repeated failures. |
| UAUTH-06 | Session management | Idle timeout (15 min), absolute timeout (8 hours), secure cookie flags (HttpOnly, Secure, SameSite=Strict). |
| UAUTH-07 | Multi-factor authentication | MFA required for admin-role users. TOTP or hardware key. |

### 9.2 Role-Based Access Control (RBAC)

| Role | Description | Permissions |
|---|---|---|
| **admin** | System administrator | Full access: create/delete users, manage roles, all sensor operations, view logs, manage certificates |
| **operator** | Day-to-day operator | Read/write sensor config (CONFIG_IP), view telemetry, view errors. Cannot manage users or roles. |
| **viewer** | Read-only monitoring | View sensor table, telemetry, connection details. No write operations. |
| **sensor** | Machine identity (per-sensor) | Publish telemetry, receive config. Identified by client certificate CN. |
| **controller** | Service identity | Bridge DB notifications to MQTT. Internal process, no interactive login. |

**Test vectors**:

| ID | Test | Description |
|---|---|---|
| RBAC-01 | Privilege escalation (horizontal) | Operator attempts to modify a sensor outside their assigned group |
| RBAC-02 | Privilege escalation (vertical) | Viewer attempts POST /api/config_ip |
| RBAC-03 | Role assignment tampering | Attempt to self-assign admin role via API |
| RBAC-04 | Token scope validation | Use a viewer token to call a write endpoint |
| RBAC-05 | Role revocation propagation | Revoke operator role and verify immediate denial on next request |

### 9.3 PostgreSQL Database Roles and Table Access

The application must use dedicated PostgreSQL roles with least-privilege access.
Never connect with the `postgres` superuser in production.

#### Database Roles

| PG Role | Used By | Privileges |
|---|---|---|
| `app_server` | Server process | SELECT, INSERT, UPDATE on `sensor_config`, `sensor_config_pending`, `sensor_state`. USAGE on `request_id_seq`. No DELETE, no DDL. |
| `app_controller` | Controller process | SELECT, INSERT, UPDATE on `sensor_config`, `sensor_state`. LISTEN on `config_requested`. No access to `sensor_config_pending`. |
| `app_readonly` | Monitoring / dashboards | SELECT only on all sensor tables. No write, no LISTEN. |
| `app_admin` | Migration scripts only | Full DDL (CREATE, ALTER, DROP). Used only during schema migrations, never at runtime. |

#### Table Access Matrix

| Table | app_server | app_controller | app_readonly | app_admin |
|---|---|---|---|---|
| `sensor_config` | SELECT, UPDATE | SELECT, INSERT, UPDATE | SELECT | ALL |
| `sensor_config_pending` | SELECT, INSERT | -- | SELECT | ALL |
| `sensor_state` | SELECT | SELECT, INSERT, UPDATE | SELECT | ALL |
| `sensor_certs` | SELECT | SELECT | SELECT | ALL |
| `request_id_seq` | USAGE (nextval) | -- | -- | ALL |

#### Security Policies

| ID | Policy | Description |
|---|---|---|
| DBPOL-01 | Connection encryption | All PostgreSQL connections must use TLS (`sslmode=verify-full` in connection string). |
| DBPOL-02 | pg_hba.conf | Reject `trust` and `password` methods. Allow only `scram-sha-256` or `cert`. |
| DBPOL-03 | Row-level security (RLS) | Future: operator users can only access sensors assigned to their group. |
| DBPOL-04 | Schema ownership | Tables owned by `app_admin`. Runtime roles granted minimum required privileges. |
| DBPOL-05 | Audit logging | Enable `pgaudit` extension to log all DDL and DML from runtime roles. |
| DBPOL-06 | Connection limits | `ALTER ROLE app_server CONNECTION LIMIT 10`. Prevent connection pool exhaustion. |
| DBPOL-07 | Statement timeout | `SET statement_timeout = '5s'` for runtime roles. Prevent long-running queries from blocking. |

**Test vectors**:

| ID | Test | Description |
|---|---|---|
| DBSEC-01 | Superuser access denied | Verify runtime application never connects as `postgres` |
| DBSEC-02 | DDL from runtime role | Attempt `DROP TABLE` from `app_server` role |
| DBSEC-03 | Cross-role data access | Attempt `app_controller` accessing `sensor_config_pending` |
| DBSEC-04 | Sequence manipulation | Attempt `setval('request_id_seq', 0)` from `app_server` |
| DBSEC-05 | Unencrypted connection | Attempt `sslmode=disable` and verify rejection |
| DBSEC-06 | pg_hba trust bypass | Attempt local connection with `trust` auth method |

---

## 10. Priority Matrix

| Priority | Category | Reason |
|---|---|---|
| **P0 - Critical** | SQL injection (SQLI-01..05) | Direct database compromise |
| **P0 - Critical** | MQTT auth bypass (MQTT-01..03) | Unauthorized config changes to sensors |
| **P0 - Critical** | Sensor impersonation (SENS-01, SENS-05) | Physical infrastructure compromise |
| **P0 - Critical** | DB role violation (DBSEC-01..06) | Privilege escalation to superuser |
| **P0 - Critical** | User auth bypass (UAUTH-01..03) | Unauthorized system access |
| **P1 - High** | Privilege escalation (RBAC-01..05) | Unauthorized operations |
| **P1 - High** | XSS (XSS-01..04) | Credential theft, session hijacking |
| **P1 - High** | TLS downgrade (NET-01, NET-02) | Encryption bypass |
| **P1 - High** | Certificate validation (AUTH-01..05) | Authentication bypass |
| **P2 - Medium** | DoS vectors (NET-04..05, PIPE-02) | Service availability |
| **P2 - Medium** | CVE scanning (all) | Known vulnerability exposure |
| **P2 - Medium** | DB connection security (DBPOL-01..07) | Defence in depth |
| **P3 - Low** | Log injection (LOG-02) | Forensic integrity |
| **P3 - Low** | CSRF (CSRF-01..02) | Requires user interaction |

---

## 11. CCN-STIC-812: Web Security Design and Methodology (ENS)

Reference: *CCN-STIC-812 -- Seguridad en Entornos y Aplicaciones Web*
(Centro Criptologico Nacional, Esquema Nacional de Seguridad).

This section summarizes the design principles and testing methodology
prescribed by the Spanish National Cryptologic Centre for web application
security. Only the abstraction layers are included here; procedural details
are deferred to the full standard.

### 11.1 Security Strategy Components

The STIC-812 strategy requires all of the following elements working together:

| Component | Description |
|---|---|
| **Security training** | Administrators and developers must understand web attack techniques and defences through practical examples. |
| **Secure architecture** | Three-tier separation (web server, application server, database), WAF between tiers, IDS at perimeter. |
| **Secure SDLC** | Security integrated into the software development lifecycle (CLASP/OWASP), not added post-deployment. |
| **Black-box analysis** | External penetration testing simulating an attacker with no prior knowledge. |
| **White-box analysis** | Internal code review for vulnerable functions, missing validations, and insecure patterns. |
| **Incident response** | Defined procedures, responsibilities, and coordination with CCN-CERT. |

### 11.2 Vulnerability Abstractions (OWASP/STIC-812)

The standard identifies these as the primary web vulnerability classes:

| Abstraction | Attack Surface | Defence |
|---|---|---|
| **XSS (Cross-Site Scripting)** | Unvalidated user data reflected/stored in HTML output | Input normalization + output encoding; CSP headers |
| **SQL Injection** | User input passed to SQL interpreter without parameterization | Parameterized queries (prepared statements); centralized filtering library |
| **CSRF (Cross-Site Request Forgery)** | Pre-authenticated browser sessions exploited by external pages | Anti-CSRF tokens per session/form; SameSite cookies; CAPTCHA on critical actions |
| **Remote File Inclusion (RFI)** | User-controlled file paths accepted by the application | Whitelist allowed paths; disable remote includes |
| **Insecure Direct Object References** | Internal IDs (files, DB records) exposed in URLs or forms | Access control checks on every reference; indirect mapping |
| **Information Leakage** | Detailed error messages, stack traces, version banners | Custom error pages; suppress server/framework banners |
| **Broken Authentication** | Weak credentials, predictable session tokens, no lockout | Cryptographic session IDs; lockout policy; MFA |
| **Insecure Cryptographic Storage** | Plaintext secrets in database or config files | Hash credentials (bcrypt/argon2); encrypt sensitive data at rest |
| **Insecure Communications** | HTTP for sensitive data; weak TLS configurations | Enforce HTTPS/TLS 1.3; HSTS; disable legacy protocols |
| **Failure to Restrict URL Access** | Authorization bypassed by directly requesting hidden URLs | Server-side access control on every endpoint; deny by default |

### 11.3 Input Filtering Design (Defence in Depth)

The standard mandates a centralized filtering library applied to all user
input before processing:

1. **Normalize first, filter second** -- Convert all encodings (URL hex,
   Unicode, HTML entities) to a canonical form before applying filters.
2. **Whitelist model preferred** -- Accept only known-valid characters for
   each field type rather than blacklisting known-bad patterns.
3. **Filter at both layers** -- Client-side validation for UX; server-side
   validation as the authoritative enforcement. Client-side alone is never
   sufficient.
4. **Centralized library** -- One shared module for all filtering functions
   (XSS, SQLi, path traversal, command injection, HTTP response splitting).
   All application entry points must route through this library.

Filtering categories:

| Category | Characters / Patterns to control |
|---|---|
| XSS | `<script>`, `<iframe>`, `<img src=`, event handlers (`onerror`, `onload`), `<`, `>`, `"`, `'`, `&`, `;` and their encoded forms |
| SQL Injection | `'`, `"`, `;`, `--`, `#`, `/*`, `*/`, `%`, `_`, `OR`, `UNION`, `SELECT`, `INSERT`, `DROP`, `CHAR()`, hex encodings |
| Path Traversal | `..`, `/`, `\`, `%c0%af`, `%c1%9c`, `%255c` (Unicode/double-encoding variants) |
| Command Injection | `;`, `>`, `<`, `\|`, `&`, `` ` `` |
| HTTP Response Splitting | `\r` (CR), `\n` (LF), `\r\n` (CRLF) |
| LDAP / XPath | `&`, `\|`, `!`, `*`, `(`, `)`, `<=`, `>=`, `~=` |

### 11.4 Secure Architecture (Three-Tier Model)

The standard prescribes a three-tier architecture for web applications,
which maps to our system as follows:

| STIC-812 Tier | Our System | Security Requirement |
|---|---|---|
| **Web Server** | QuantumSafeTlsEngine + QuantumSafeHttp | TLS v1.3 PQC termination; WAF filtering; input validation |
| **Application Server** | Dispatcher + CLI/HTTP handlers | Business logic isolation; no direct DB access from transport layer |
| **Database** | PostgreSQL | Encrypted connections (sslmode=verify-full); least-privilege roles; parameterized queries only |

Additional STIC-812 requirements:

- WAF between each tier (at minimum, between external clients and web server).
- IDS/IPS at network perimeter monitoring TLS-terminated traffic.
- All inter-tier communications encrypted and authenticated.
- Ingress and egress filtering on all perimeter firewalls.
- Documented credentials and permissions for every component-to-component
  interaction.

### 11.5 Testing Methodology (Black-Box Phases)

The standard defines three sequential phases for external security audits:

**Phase 1 -- Reconnaissance**
- DNS records, WHOIS, domain registration
- Search engine information leakage (Google Hacking / dorking)
- Full sitemap generation (static and dynamic content)
- Network topology and perimeter device identification

**Phase 2 -- Scanning**
- TCP/UDP port enumeration on all exposed hosts
- Service fingerprinting (web server, application framework, database)
- OS fingerprinting
- SSL/TLS configuration analysis (versions, ciphers, certificates, CA chain)
- HTTP method enumeration (especially TRACE -> XST)
- Identification of default resources and administrative interfaces

**Phase 3 -- Vulnerability Analysis**
- Input parameter testing against all dynamic endpoints for: SQLi, blind SQLi,
  XSS (reflected + stored), CSRF, HTTP response splitting, command injection,
  path traversal, file inclusion, buffer overflow, CAPTCHA bypass
- Authentication mechanism analysis: credential strength, lockout policy,
  enumeration resistance, session token entropy and lifecycle
- Session management: token format, expiration, fixation, hijacking, cookie
  flags (Secure, HttpOnly, SameSite)
- Access control: ACL enforcement, privilege escalation, direct URL access
- Load testing / DoS (optional): single-client and distributed, resource
  exhaustion, account lockout flooding

**White-Box (Code Review) Areas**:
- Buffer overflows
- OS command injection
- SQL injection in dynamic queries
- Input validation completeness
- XSS and CSRF protection
- Error handling and information disclosure
- Authentication and session management
- Authorization checks
- Cryptographic usage (storage and transit)
- Race conditions
- Log management

### 11.6 Audit Results Requirements (STIC-812 Section 5)

All security audit results must satisfy:

| Requirement | Description |
|---|---|
| **Completeness** | Reflects the real and full security state of all web services, regardless of technology |
| **Relevance** | Demonstrates practical impact on Availability, Integrity, Confidentiality, Authenticity, and Traceability |
| **Confidentiality** | All auditors under NDA with financial penalties; full personnel disclosure |
| **Reproducibility** | All findings accompanied by evidence: captured pages, PCAP files, vulnerable URL lists, timestamps |
| **Classification** | Vulnerabilities listed and ranked by criticality |

### 11.7 References

- CCN-STIC-812 v1.0 -- Seguridad en Entornos y Aplicaciones Web (ENS)
- CCN-STIC-661 -- Seguridad en Firewalls de Aplicacion
- CCN-STIC-408 -- Seguridad Perimetral - Cortafuegos
- CCN-STIC-432 -- Seguridad Perimetral - IDS
- CCN-STIC-810 -- Creacion de CERTs
- CCN-STIC-817 -- Gestion de Incidentes de Seguridad en el ENS
- CCN-STIC-818 -- Herramientas de Seguridad
- OWASP Top 10
- OWASP CLASP (Comprehensive, Lightweight Application Security Process)
- OWASP Secure Software Contract Annex
- WASC -- Web Application Security Consortium (WHID database)
