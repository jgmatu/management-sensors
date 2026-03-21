# Auth UI seed (→ `private/components/auth/`)

On each `pnpm dev` / `pnpm build`, `scripts/setup-components.sh` **overwrites**
`private/components/auth/*.tsx` from this folder, then copies all of `private/components/`
into `frontend/src/components/`.

Customize the login UI by editing these tracked files under **`auth-seed/`** (not by
editing only `private/`, which would be replaced on the next setup).
