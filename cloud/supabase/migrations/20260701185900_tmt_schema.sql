-- Schema TMT (Chunk F) — persistência de telemetria e eventos (PLAN §6, RF11).
-- Tabelas: devices, readings, events. Alinhadas ao contrato MQTT do firmware.

create table if not exists public.devices (
    device_id  text primary key,
    name       text,
    location   text,
    thresholds jsonb,                 -- override de limiares por dispositivo (RN03)
    last_seen  timestamptz            -- atualizado pelo bridge a cada mensagem (RF07)
);

create table if not exists public.readings (
    id         uuid primary key default gen_random_uuid(),
    device_id  text not null references public.devices(device_id) on delete cascade,
    ts         bigint not null,       -- epoch em segundos (relógio do device via SNTP)
    temp_c     double precision,      -- °C  (null se leitura inválida)
    hum_pct    double precision,      -- %UR (null se inválida)
    light      integer,               -- leitura ADC bruta do LDR (null se inválida)
    mains_ok   boolean,
    created_at timestamptz default now()
);
create index if not exists readings_device_ts_idx on public.readings (device_id, ts desc);

create table if not exists public.events (
    id         uuid primary key default gen_random_uuid(),
    device_id  text not null references public.devices(device_id) on delete cascade,
    ts         bigint not null,
    type       text not null,         -- thermal | humidity | door | panic | power
    state      text,                  -- E1 | E2 | E5 ...
    severity   text,                  -- INFO | WARN | CRIT
    value      double precision,
    threshold  double precision,
    normalized boolean default false, -- true = retorno à faixa normal (RF09)
    created_at timestamptz default now()
);
create index if not exists events_device_ts_idx on public.events (device_id, ts desc);

-- RLS habilitada em todas as tabelas (RN05/LGPD). A service_role usada pelo bridge
-- ignora RLS; políticas de leitura escopadas para dashboards ficam como trabalho futuro.
alter table public.devices  enable row level security;
alter table public.readings enable row level security;
alter table public.events   enable row level security;

-- Retenção ≥12 meses (RNF08). Em produção, agendar via pg_cron; aqui deixamos a
-- função pronta para ser chamada por um job.
create or replace function public.tmt_purge_old(retention interval default interval '12 months')
returns void language sql as $$
    delete from public.readings where to_timestamp(ts) < now() - retention;
    delete from public.events   where to_timestamp(ts) < now() - retention;
$$;
