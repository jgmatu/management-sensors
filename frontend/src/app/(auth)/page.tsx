import type { Metadata } from "next";
import { AuthPageTemplate } from "../../components/auth/AuthPageTemplate";

export const metadata: Metadata = {
  title: "Acceso | Management Sensors",
  description: "Entrada a la aplicación",
};

/**
 * Página de entrada (SPA): autenticación — plantilla vacía desde private/components/auth.
 */
export default function AuthEntryPage() {
  return <AuthPageTemplate />;
}
