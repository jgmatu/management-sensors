"use client";

import { useState, type FormEvent } from "react";
import { Button } from "../button";
import { Field, FieldGroup, Label } from "../fieldset";
import { Input } from "../input";

/**
 * Login form using private Catalyst-style components (fieldset, input, button).
 * Wire `onSubmit` to POST /api/auth/login when the API base URL is configured.
 */
export function AuthLoginForm() {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");

  function handleSubmit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault();
    // TODO: fetch JWT from server (e.g. POST /api/auth/login)
    console.info("[auth] submit (mock)", { username, passwordLen: password.length });
  }

  return (
    <form onSubmit={handleSubmit} className="mt-8 space-y-6">
      <FieldGroup>
        <Field>
          <Label htmlFor="auth-username">Usuario</Label>
          <Input
            id="auth-username"
            name="username"
            type="text"
            autoComplete="username"
            value={username}
            onChange={(e) => setUsername(e.target.value)}
            required
          />
        </Field>
        <Field>
          <Label htmlFor="auth-password">Contraseña</Label>
          <Input
            id="auth-password"
            name="password"
            type="password"
            autoComplete="current-password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            required
          />
        </Field>
      </FieldGroup>
      <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
        <Button type="submit" color="dark/zinc" className="w-full sm:w-auto">
          Entrar
        </Button>
        <button
          type="button"
          className="text-center text-sm text-zinc-500 underline-offset-4 hover:text-zinc-800 hover:underline dark:text-zinc-400 dark:hover:text-zinc-200"
        >
          ¿Olvidó su contraseña?
        </button>
      </div>
    </form>
  );
}
