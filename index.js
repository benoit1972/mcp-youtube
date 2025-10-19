import { serve } from "bun";

serve({
  port: 8080,
  fetch(req) {
    const url = new URL(req.url);

    if (url.pathname === "/") {
      return new Response("MCP YouTube server is running", {
        headers: { "Content-Type": "text/plain" },
      });
    }

    return new Response("Not found", { status: 404 });
  },
});

console.log("✅ MCP YouTube server running on http://localhost:8080");