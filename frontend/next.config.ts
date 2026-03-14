import type { NextConfig } from "next";

const nextConfig: NextConfig = {
    output: 'export', // Enables static export
    trailingSlash: true, // Adds a trailing slash to all routes
};

export default nextConfig;
