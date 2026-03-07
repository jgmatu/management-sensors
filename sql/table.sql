-- 1. Create the articles table
CREATE TABLE IF NOT EXISTS articles (
    id SERIAL PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    stock_count INTEGER NOT NULL DEFAULT 0,
    updated_at TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

DROP TRIGGER IF EXISTS trg_articles_notify ON articles;

CREATE OR REPLACE FUNCTION articles_notify() RETURNS trigger AS $$
DECLARE
  payload jsonb;
  rec record;
BEGIN
  -- Use OLD for DELETE, NEW for INSERT/UPDATE
  rec := CASE WHEN TG_OP = 'DELETE' THEN OLD ELSE NEW END;

  payload = jsonb_build_object(
    'id', rec.id,
    'name', rec.name,
    'op', TG_OP,
    'stock', rec.stock_count
  );

  -- Changed channel from 'articles_channel' to 'events'
  PERFORM pg_notify('events', payload::text);

  RETURN NULL;
END; $$ LANGUAGE plpgsql;

CREATE TRIGGER trg_articles_notify
AFTER INSERT OR UPDATE OR DELETE ON articles
FOR EACH ROW EXECUTE FUNCTION articles_notify();

-- Seed with some initial data
INSERT INTO articles (name, stock_count) VALUES
  ('cafe', 50),
  ('balloons', 100),
  ('espresso', 25) ON CONFLICT (name) DO NOTHING;
