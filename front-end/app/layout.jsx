import "./globals.css";

export const metadata = {
  title: "LoveBox 💌",
  description: "Send little love notes to the boxes",
};

export default function RootLayout({ children }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
