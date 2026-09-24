// 로그인한 사용자 (토큰 권한)
export const session = { login: '', scope: 'read', owner_id: '' };
export const canControl = () => session.scope === 'control' || session.scope === 'admin';
