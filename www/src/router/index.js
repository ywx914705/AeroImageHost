import { createRouter, createWebHistory } from 'vue-router'
import { useAppStore } from '../stores/app'

const routes = [
  {
    path: '/',
    component: () => import('../layouts/MainLayout.vue'),
    children: [
      { path: '', name: 'dashboard', component: () => import('../views/Dashboard.vue') },
      { path: 'upload', name: 'upload', component: () => import('../views/Upload.vue') },
      { path: 'library', name: 'library', component: () => import('../views/Library.vue') },
      { path: 'library/:folderId', name: 'folder', component: () => import('../views/Library.vue') },
      { path: 'hall', name: 'hall', component: () => import('../views/Hall.vue') },
      { path: 'favorites', name: 'favorites', component: () => import('../views/Favorites.vue') },
    ]
  },
  {
    path: '/login',
    name: 'login',
    component: () => import('../views/Login.vue')
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach((to, from, next) => {
  const appStore = useAppStore()
  if (to.name !== 'login' && !appStore.token) {
    next({ name: 'login' })
  } else {
    next()
  }
})

export default router
