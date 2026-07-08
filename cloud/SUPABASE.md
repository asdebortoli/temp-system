# Supabase (TMT) — persistência local

Stack Supabase **local em Docker** (Chunk F). Guarda telemetria e eventos que o
bridge (Chunk G) recebe do broker MQTT.

## Subir

```bash
cd cloud/supabase
supabase start          # sobe Postgres + Studio + PostgREST (Docker)
supabase status         # mostra API URL, Studio URL e as chaves (anon / service_role)
```

- **API URL**: `http://127.0.0.1:54321`
- **Studio**: `http://127.0.0.1:54323`
- **service_role key**: use no bridge (`SUPABASE_KEY`). É a chave que ignora RLS.

Migrations e seed são aplicados no `supabase start` (primeira vez). Para reaplicar do
zero: `supabase db reset`.

## Schema (migration `20260701185900_tmt_schema.sql`)

| Tabela     | Campos principais                                                        |
|------------|--------------------------------------------------------------------------|
| `devices`  | `device_id` PK, `name`, `location`, `thresholds` jsonb, `last_seen`      |
| `readings` | `id`, `device_id` FK, `ts` epoch, `temp_c`, `hum_pct`, `light`, `mains_ok` |
| `events`   | `id`, `device_id` FK, `ts`, `type`, `state`, `severity`, `value`, `threshold`, `normalized` |

Índices `(device_id, ts desc)` em `readings` e `events` para consultas de série temporal.
Seed cria o device `tmt-dev-01`.

## Retenção (RNF08)

Função `public.tmt_purge_old(interval)` remove `readings`/`events` mais antigos que a
janela (padrão 12 meses). Em produção, agendar via `pg_cron`:

```sql
select cron.schedule('tmt-purge', '0 3 * * *', $$ select public.tmt_purge_old(); $$);
```

Localmente pode ser chamada à mão: `select public.tmt_purge_old();`.

## Export CSV (RN04 / RF11)

```bash
# via psql do container (porta padrão 54322)
psql "postgresql://postgres:postgres@127.0.0.1:54322/postgres" \
  -c "\copy (select * from public.readings order by ts) to 'readings.csv' csv header"
psql "postgresql://postgres:postgres@127.0.0.1:54322/postgres" \
  -c "\copy (select * from public.events order by ts) to 'events.csv' csv header"
```

Ou pelo Studio: SQL Editor → `select * from readings` → *Download CSV*.

## LGPD (RN05)

- **Minimização**: `readings`/`events` não guardam dados pessoais; `location` é opcional e
  descreve o ambiente, não pessoas.
- **Titularidade**: os dados pertencem ao estabelecimento dono do dispositivo.
- **Controle de acesso**: RLS habilitada; o bridge usa a `service_role` (backend confiável).
  Políticas de leitura escopadas para dashboards de usuário são trabalho futuro.
- **Direito à exclusão**: `delete from devices where device_id = '...'` remove o device e,
  por `on delete cascade`, todas as suas leituras e eventos.
