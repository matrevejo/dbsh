-- ==============================================================================
-- Script de inicialización de la Base de Datos Sandbox para DBSH
-- ==============================================================================

-- 1. Crear base de datos de pruebas (si no existe)
SELECT 'CREATE DATABASE dbsh_sandbox'
WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'dbsh_sandbox')\gexec

\c dbsh_sandbox

-- 2. Crear roles para acceso local (peer) y remoto (con contraseña)
DO $$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'colpasus') THEN
    CREATE ROLE colpasus WITH LOGIN SUPERUSER;
  END IF;
  IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'dbsh_user') THEN
    CREATE ROLE dbsh_user WITH LOGIN PASSWORD 'dbsh_pass';
  END IF;
END
$$;

-- 3. Tabla: servers (infraestructura)
CREATE TABLE IF NOT EXISTS servers (
    id SERIAL PRIMARY KEY,
    hostname VARCHAR(64) NOT NULL,
    ip_address VARCHAR(45) NOT NULL,
    environment VARCHAR(20) DEFAULT 'development',
    status VARCHAR(20) DEFAULT 'online',
    cpu_cores INT,
    ram_gb INT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 4. Tabla: users (usuarios y roles)
CREATE TABLE IF NOT EXISTS users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) NOT NULL UNIQUE,
    email VARCHAR(100) NOT NULL,
    role VARCHAR(20) DEFAULT 'operator',
    active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 5. Tabla: metrics (métricas de monitoreo)
CREATE TABLE IF NOT EXISTS metrics (
    id SERIAL PRIMARY KEY,
    server_id INT REFERENCES servers(id) ON DELETE CASCADE,
    cpu_usage NUMERIC(5,2),
    ram_usage NUMERIC(5,2),
    disk_usage NUMERIC(5,2),
    recorded_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 6. Insertar datos de prueba
INSERT INTO servers (hostname, ip_address, environment, status, cpu_cores, ram_gb) VALUES
('srv-web-01', '192.168.1.10', 'production', 'online', 8, 32),
('srv-db-master', '192.168.1.20', 'production', 'online', 16, 64),
('srv-db-replica', '192.168.1.21', 'production', 'replicating', 16, 64),
('srv-cache-01', '192.168.1.30', 'production', 'online', 4, 16),
('srv-dev-sandbox', '192.168.1.99', 'staging', 'offline', 2, 8)
ON CONFLICT DO NOTHING;

INSERT INTO users (username, email, role, active) VALUES
('admin', 'admin@dbsh.local', 'superuser', true),
('dev_lead', 'developer@dbsh.local', 'developer', true),
('qa_tester', 'tester@dbsh.local', 'tester', true),
('dba_support', 'dba@dbsh.local', 'admin', true),
('guest', 'guest@dbsh.local', 'readonly', false)
ON CONFLICT (username) DO NOTHING;

INSERT INTO metrics (server_id, cpu_usage, ram_usage, disk_usage) VALUES
(1, 14.50, 42.10, 58.30),
(2, 65.20, 78.90, 62.40),
(3, 22.10, 51.00, 61.90),
(4, 5.30, 88.40, 12.00)
ON CONFLICT DO NOTHING;

-- 7. Conceder permisos completos a colpasus y dbsh_user
GRANT ALL PRIVILEGES ON DATABASE dbsh_sandbox TO colpasus, dbsh_user;
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO colpasus, dbsh_user;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO colpasus, dbsh_user;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON TABLES TO colpasus, dbsh_user;
ALTER DEFAULT PRIVILEGES IN SCHEMA public GRANT ALL ON SEQUENCES TO colpasus, dbsh_user;
