import { AuthLoginForm } from "./AuthLoginForm";

/**
 * Authentication page shell — layout copy lives in private/components/auth/.
 * Form uses private UI components (fieldset, input, button) synced via setup-components.sh.
 */
export function AuthPageTemplate() {
  return (
    <div className="w-full max-w-md rounded-xl border border-zinc-200 bg-white p-8 shadow-sm dark:border-zinc-800 dark:bg-zinc-900">
      <h1 className="text-xl font-semibold tracking-tight">Acceso</h1>
      <p className="mt-2 text-sm text-zinc-600 dark:text-zinc-400">
        Inicie sesión para continuar. El envío real contra el servidor (JWT) se conectará
        en el controlador del formulario.
      </p>
      <AuthLoginForm />
    </div>
  );
}
